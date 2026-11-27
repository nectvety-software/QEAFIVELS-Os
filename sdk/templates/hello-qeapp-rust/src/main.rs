#![cfg_attr(not(qeapp_combined_crate), no_std)]

#[cfg(qeapp_combined_crate)]
use crate::qeapp as qeapp_sdk;

#[cfg(not(qeapp_combined_crate))]
extern crate qeapp_sdk;

use qeapp_sdk::prelude::*;

#[cfg(not(qeapp_combined_crate))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo<'_>) -> ! {
    loop {}
}

struct HelloApp {
    api: ApiV5,
    started: bool,
}

impl QeappApp for HelloApp {
    type Api = ApiV5;

    fn new(api: &ApiV5) -> Self {
        Self {
            api: *api,
            started: false,
        }
    }

    fn start(&mut self) -> i32 {
        if self.started {
            return 0;
        }
        self.started = true;
        if unsafe { self.api.log("Hello Rust QEAPP: start") }.is_err() {
            return -1;
        }
        let result = unsafe {
            self.api.draw_text(48, 120, "Hello from Rust QEAPP v5")
        };
        if result.is_ok() {
            0
        } else {
            -1
        }
    }

    fn stop(&mut self) -> i32 {
        self.started = false;
        if unsafe { self.api.log("Hello Rust QEAPP: stop") }.is_ok() {
            0
        } else {
            -1
        }
    }
}

qeapp_sdk::qeapp_app!(HelloApp);
