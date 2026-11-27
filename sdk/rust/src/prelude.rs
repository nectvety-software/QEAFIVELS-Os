#[cfg(not(qeapp_combined_crate))]
pub use crate::api::*;
#[cfg(not(qeapp_combined_crate))]
pub use crate::ffi::QeappApp;
#[cfg(qeapp_combined_crate)]
pub use crate::qeapp::api::*;
#[cfg(qeapp_combined_crate)]
pub use crate::qeapp::ffi::QeappApp;
