#![allow(unexpected_cfgs)]
#![cfg_attr(not(qeapp_combined_crate), no_std)]

extern crate self as qeapp_sdk;

pub mod api;
pub mod ffi;
pub mod prelude;

#[cfg(qeapp_combined_crate)]
mod runtime;

#[cfg(qeapp_combined_crate)]
pub use crate::qeapp_app;
