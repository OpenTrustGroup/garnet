// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

// dummy main. We do not copy this binary to fuchsia, only tests.
fn main() {}

#[cfg(test)]
mod tests {
    use fidl_fuchsia_logger::{LogFilterOptions, LogLevelFilter, LogMessage};
    use fuchsia_async as fasync;
    use fuchsia_syslog::{self as syslog, fx_log, fx_log_info};
    use fuchsia_syslog_listener::{self as syslog_listener, LogProcessor};
    use fuchsia_zircon::DurationNum;
    use futures::TryFutureExt;
    use log::{log, warn};
    use parking_lot::Mutex;

    use std::sync::Arc;
    use std::vec::Vec;

    struct Listener {
        log_messages: Arc<Mutex<Vec<LogMessage>>>,
    }

    impl LogProcessor for Listener {
        fn log(&mut self, message: LogMessage) {
            self.log_messages.lock().push(message);
        }

        fn done(&mut self) {
            panic!("this should not be called");
        }
    }

    fn run_listener(tag: String) -> Arc<Mutex<Vec<LogMessage>>> {
        let mut options = LogFilterOptions {
            filter_by_pid: false,
            pid: 0,
            min_severity: LogLevelFilter::None,
            verbosity: 0,
            filter_by_tid: false,
            tid: 0,
            tags: vec![tag],
        };
        let logs = Arc::new(Mutex::new(Vec::new()));
        let l = Listener {
            log_messages: logs.clone(),
        };
        let listener_fut = syslog_listener::run_log_listener(l, Some(&mut options), false)
            .expect("failed to register listener");
        fasync::spawn(listener_fut.unwrap_or_else(|e| {
            panic!("test fail {:?}", e);
        }));
        return logs;
    }

    #[test]
    fn test_full_stack() {
        let mut executor = fasync::Executor::new().unwrap();
        let random = rand::random::<u16>();
        let tag = "logger_integration_rust".to_string() + &random.to_string();
        syslog::init_with_tags(&[&tag]).expect("should not fail");
        let logs = run_listener(tag.clone());
        fx_log_info!("my msg: {}", 10);
        warn!("log crate: {}", 20);

        let tries = 50;
        for _ in 0..tries {
            if logs.lock().len() >= 2 {
                break;
            }
            let timeout = fasync::Timer::new(100.millis().after_now());
            executor.run_singlethreaded(timeout);
        }
        let logs = logs.lock();
        assert_eq!(2, logs.len());
        assert_eq!(logs[0].tags, vec![tag.clone()]);
        assert_eq!(logs[0].severity, LogLevelFilter::Info as i32);
        assert_eq!(logs[0].msg, "my msg: 10");

        assert_eq!(logs[1].tags[0], tag.clone());
        assert_eq!(logs[1].severity, LogLevelFilter::Warn as i32);
        assert_eq!(logs[1].msg, "log crate: 20");
    }
}
