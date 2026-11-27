#[cfg(not(qeapp_combined_crate))]
use crate::api::{ApiTable, InputEvent};
#[cfg(qeapp_combined_crate)]
use crate::qeapp::api::{ApiTable, InputEvent};

pub trait QeappApp: Sized {
    type Api: ApiTable;

    fn new(api: &Self::Api) -> Self;
    fn start(&mut self) -> i32 {
        0
    }
    fn pause(&mut self) -> i32 {
        0
    }
    fn resume(&mut self) -> i32 {
        0
    }
    fn stop(&mut self) -> i32 {
        0
    }
    fn on_input(&mut self, _event: &InputEvent) {}
}

#[macro_export]
macro_rules! qeapp_app {
    ($app:ty) => {
        static mut QEAPP_INSTANCE: Option<$app> = None;

        #[no_mangle]
        pub unsafe extern "C" fn create(
            api: *const $crate::qeapp_sdk::api::ApiV2Raw,
        ) -> *mut core::ffi::c_void {
            let raw = api.cast::<<$app as $crate::qeapp_sdk::ffi::QeappApp>::Api>();
            let api =
                match unsafe { <$app as $crate::qeapp_sdk::ffi::QeappApp>::Api::from_raw(raw) } {
                    Ok(api) => api,
                    Err(_) => return core::ptr::null_mut(),
                };
            let instance = <$app as $crate::qeapp_sdk::ffi::QeappApp>::new(&api);
            core::ptr::addr_of_mut!(QEAPP_INSTANCE).write(Some(instance));
            core::ptr::addr_of_mut!(QEAPP_INSTANCE).cast::<core::ffi::c_void>()
        }

        #[no_mangle]
        pub unsafe extern "C" fn start(_handle: *mut core::ffi::c_void) -> i32 {
            let slot = core::ptr::addr_of_mut!(QEAPP_INSTANCE);
            match unsafe { (*slot).as_mut() } {
                Some(app) => <$app as $crate::qeapp_sdk::ffi::QeappApp>::start(app),
                None => -1,
            }
        }

        #[no_mangle]
        pub unsafe extern "C" fn pause(_handle: *mut core::ffi::c_void) -> i32 {
            let slot = core::ptr::addr_of_mut!(QEAPP_INSTANCE);
            match unsafe { (*slot).as_mut() } {
                Some(app) => <$app as $crate::qeapp_sdk::ffi::QeappApp>::pause(app),
                None => -1,
            }
        }

        #[no_mangle]
        pub unsafe extern "C" fn resume(_handle: *mut core::ffi::c_void) -> i32 {
            let slot = core::ptr::addr_of_mut!(QEAPP_INSTANCE);
            match unsafe { (*slot).as_mut() } {
                Some(app) => <$app as $crate::qeapp_sdk::ffi::QeappApp>::resume(app),
                None => -1,
            }
        }

        #[no_mangle]
        pub unsafe extern "C" fn stop(_handle: *mut core::ffi::c_void) -> i32 {
            let slot = core::ptr::addr_of_mut!(QEAPP_INSTANCE);
            match unsafe { (*slot).as_mut() } {
                Some(app) => <$app as $crate::qeapp_sdk::ffi::QeappApp>::stop(app),
                None => -1,
            }
        }

        #[no_mangle]
        pub unsafe extern "C" fn destroy(_handle: *mut core::ffi::c_void) {
            let slot = core::ptr::addr_of_mut!(QEAPP_INSTANCE);
            unsafe {
                (*slot).take();
            }
        }

        #[no_mangle]
        pub unsafe extern "C" fn on_input(
            _handle: *mut core::ffi::c_void,
            event: *const $crate::qeapp_sdk::api::InputEvent,
        ) {
            if event.is_null() {
                return;
            }
            let slot = core::ptr::addr_of_mut!(QEAPP_INSTANCE);
            if let Some(app) = unsafe { (*slot).as_mut() } {
                <$app as $crate::qeapp_sdk::ffi::QeappApp>::on_input(app, unsafe { &*event });
            }
        }
    };
}
