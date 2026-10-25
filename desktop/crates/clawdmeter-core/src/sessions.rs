//! The device's session list (the payload's `ss`): each Claude Code session
//! seen in the hook events, what it is doing, and since when.
//!
//! The hooks only report events, so the state is inferred. A prompt or a tool
//! call means the session is working; a notification while it works, or an
//! AskUserQuestion, means it waits on you; a Stop means the turn is done. The
//! hook logs Claude Code's idle reminder ("waiting for your input") as
//! [`IDLE_PROMPT_EVENT`]: after an interrupted turn, which sends no Stop, it
//! means the session is idle; after a Stop the session stays done. A done
//! turn reads as idle after [`DONE_SECS`], and a session with no event for
//! [`LIST_SECS`] leaves the list. A session keeps the first project name it
//! was seen with, since the hook's `cwd` follows Claude's own `cd`s.

use std::collections::HashMap;

use crate::events::{HookEvent, HookKind};

/// A session with no hook event for this long leaves the list.
pub const LIST_SECS: i64 = 1800;
/// A done turn reads as idle after this long.
pub const DONE_SECS: i64 = 120;
/// Rows the device gets at most.
pub const MAX_ROWS: usize = 4;
/// The event name the hook logs for an idle-reminder `Notification`.
pub const IDLE_PROMPT_EVENT: &str = "IdlePrompt";

/// What a session is doing. The order is the list's: what waits on you first.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum SessionState {
    /// A permission prompt or a question is up.
    Waiting,
    /// A prompt is being worked on.
    Working,
    /// The turn finished in the last [`DONE_SECS`].
    Done,
    /// Nothing going on.
    Idle,
}

impl SessionState {
    /// The one-letter payload code.
    pub fn code(self) -> &'static str {
        match self {
            SessionState::Waiting => "w",
            SessionState::Working => "r",
            SessionState::Done => "d",
            SessionState::Idle => "i",
        }
    }
}

/// One row of the list.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SessionRow {
    /// The project name, or the start of the session id when the hook didn't
    /// know the project.
    pub name: String,
    /// What it is doing.
    pub state: SessionState,
    /// Unix seconds it has been in that state since.
    pub since: i64,
}

#[derive(Debug, Clone)]
struct Tracked {
    project: Option<String>,
    state: SessionState,
    since: i64,
    last: i64,
}

/// Every session in the last [`LIST_SECS`], fed from the hook events.
#[derive(Debug, Clone, Default)]
pub struct SessionTracker {
    sessions: HashMap<String, Tracked>,
}

impl SessionTracker {
    /// Fold in one event (history at start-up, or a live one).
    pub fn record(&mut self, ev: &HookEvent) {
        if ev.session.is_empty() {
            return;
        }
        let t = self.sessions.entry(ev.session.clone()).or_insert(Tracked {
            project: None,
            state: SessionState::Idle,
            since: ev.ts,
            last: ev.ts,
        });
        if t.project.is_none() {
            t.project = ev.project.clone();
        }
        t.last = t.last.max(ev.ts);
        let next = match &ev.kind {
            HookKind::UserPromptSubmit | HookKind::PostToolUse => Some(SessionState::Working),
            HookKind::PreToolUse => Some(SessionState::Waiting),
            HookKind::Notification if t.state == SessionState::Working => Some(SessionState::Waiting),
            HookKind::Stop => Some(SessionState::Done),
            HookKind::Other(name) if name == IDLE_PROMPT_EVENT && t.state == SessionState::Working => {
                Some(SessionState::Idle)
            }
            _ => None,
        };
        if let Some(s) = next.filter(|s| *s != t.state) {
            t.state = s;
            t.since = ev.ts;
        }
    }

    /// Forget sessions that left the list.
    pub fn prune(&mut self, now: i64) {
        self.sessions.retain(|_, t| now - t.last < LIST_SECS);
    }

    /// The list as of `now`: waiting first, then working, done and idle,
    /// newest first within each, at most [`MAX_ROWS`].
    pub fn rows(&self, now: i64) -> Vec<SessionRow> {
        let mut rows: Vec<SessionRow> = self
            .sessions
            .iter()
            .filter(|(_, t)| now - t.last < LIST_SECS)
            .map(|(id, t)| {
                let (state, since) = if t.state == SessionState::Done && now - t.since >= DONE_SECS {
                    (SessionState::Idle, t.since + DONE_SECS)
                } else {
                    (t.state, t.since)
                };
                let name = t.project.clone().unwrap_or_else(|| id.chars().take(8).collect());
                SessionRow { name, state, since }
            })
            .collect();
        rows.sort_by(|a, b| a.state.cmp(&b.state).then(b.since.cmp(&a.since)).then(a.name.cmp(&b.name)));
        rows.truncate(MAX_ROWS);
        rows
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ev(ts: i64, kind: &str, sid: &str, project: Option<&str>) -> HookEvent {
        HookEvent {
            ts,
            kind: HookKind::parse(kind),
            session: sid.into(),
            project: project.map(str::to_owned),
            account: None,
        }
    }

    fn states(t: &SessionTracker, now: i64) -> Vec<(String, &'static str, i64)> {
        t.rows(now).into_iter().map(|r| (r.name, r.state.code(), now - r.since)).collect()
    }

    #[test]
    fn a_turn_works_waits_and_finishes() {
        let mut t = SessionTracker::default();
        t.record(&ev(100, "UserPromptSubmit", "s1", Some("herald")));
        t.record(&ev(110, "PostToolUse", "s1", Some("herald")));
        assert_eq!(states(&t, 120), vec![("herald".into(), "r", 20)]);
        t.record(&ev(130, "Notification", "s1", Some("herald")));
        assert_eq!(states(&t, 140), vec![("herald".into(), "w", 10)]);
        t.record(&ev(150, "PostToolUse", "s1", Some("herald"))); // approved
        assert_eq!(states(&t, 150), vec![("herald".into(), "r", 0)]);
        t.record(&ev(200, "Stop", "s1", Some("herald")));
        assert_eq!(states(&t, 210), vec![("herald".into(), "d", 10)]);
        // The idle reminder after a Stop isn't a question.
        t.record(&ev(260, "Notification", "s1", Some("herald")));
        assert_eq!(states(&t, 270), vec![("herald".into(), "d", 70)]);
        // Done turns into idle, counted from then.
        assert_eq!(states(&t, 200 + DONE_SECS + 30), vec![("herald".into(), "i", 30)]);
    }

    #[test]
    fn an_interrupted_turn_goes_idle_not_waiting() {
        let mut t = SessionTracker::default();
        t.record(&ev(100, "UserPromptSubmit", "s1", None));
        // Esc: no Stop. A minute later the idle reminder.
        t.record(&ev(160, IDLE_PROMPT_EVENT, "s1", None));
        assert_eq!(t.rows(170)[0].state, SessionState::Idle);
        // A permission prompt that's already up isn't cleared by it.
        t.record(&ev(200, "UserPromptSubmit", "s1", None));
        t.record(&ev(210, "Notification", "s1", None));
        t.record(&ev(270, IDLE_PROMPT_EVENT, "s1", None));
        assert_eq!(t.rows(280)[0].state, SessionState::Waiting);
    }

    #[test]
    fn a_question_waits() {
        let mut t = SessionTracker::default();
        t.record(&ev(100, "UserPromptSubmit", "s1", None));
        t.record(&ev(105, "PreToolUse", "s1", None));
        assert_eq!(t.rows(110)[0].state, SessionState::Waiting);
    }

    #[test]
    fn order_waiting_first_then_newest() {
        let mut t = SessionTracker::default();
        t.record(&ev(100, "Stop", "a", Some("old-done")));
        t.record(&ev(150, "UserPromptSubmit", "b", Some("busy")));
        t.record(&ev(160, "UserPromptSubmit", "c", Some("asks")));
        t.record(&ev(170, "Notification", "c", Some("asks")));
        t.record(&ev(180, "UserPromptSubmit", "d", Some("newer-busy")));
        let names: Vec<String> = t.rows(190).into_iter().map(|r| r.name).collect();
        assert_eq!(names, ["asks", "newer-busy", "busy", "old-done"]);
    }

    #[test]
    fn at_most_four_rows_and_stale_sessions_leave() {
        let mut t = SessionTracker::default();
        for i in 0..6 {
            t.record(&ev(100 + i, "UserPromptSubmit", &format!("s{i}"), None));
        }
        assert_eq!(t.rows(200).len(), MAX_ROWS);
        assert!(t.rows(105 + LIST_SECS).is_empty());
        t.prune(105 + LIST_SECS);
        assert!(t.sessions.is_empty());
    }

    #[test]
    fn unnamed_sessions_use_their_id_and_keep_a_later_name() {
        let mut t = SessionTracker::default();
        t.record(&ev(100, "UserPromptSubmit", "0123456789abcdef", None));
        assert_eq!(t.rows(100)[0].name, "01234567");
        t.record(&ev(101, "PostToolUse", "0123456789abcdef", Some("proj")));
        t.record(&ev(102, "PostToolUse", "0123456789abcdef", None));
        assert_eq!(t.rows(102)[0].name, "proj");
        // Claude cd-ing into a subfolder doesn't rename the session.
        t.record(&ev(102, "PostToolUse", "0123456789abcdef", Some("desktop")));
        assert_eq!(t.rows(102)[0].name, "proj");
        // No session id: nothing to track.
        t.record(&ev(103, "UserPromptSubmit", "", Some("x")));
        assert_eq!(t.rows(103).len(), 1);
    }
}
