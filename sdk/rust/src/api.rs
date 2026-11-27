use core::ffi::{c_char, c_void, CStr};
#[cfg(not(qeapp_combined_crate))]
use core::fmt;
use core::ptr;

pub type LogUtf8Fn = Option<unsafe extern "C" fn(*const c_char)>;
pub type DrawTextFn = Option<unsafe extern "C" fn(i32, i32, *const c_char) -> i32>;
pub type RequestPermissionFn = Option<unsafe extern "C" fn(*const c_char) -> i32>;
pub type ReadFileFn =
    Option<unsafe extern "C" fn(*const c_char, *mut *mut c_void, *mut usize) -> i32>;
pub type FreeFn = Option<unsafe extern "C" fn(*mut c_void)>;
pub type BlitPixelsFn = Option<unsafe extern "C" fn(i32, i32, i32, i32, i32, *const u16) -> i32>;
pub type ReadFilePartFn =
    Option<unsafe extern "C" fn(*const c_char, u32, usize, *mut c_void) -> i32>;
pub type DrawTextWeightedFn = Option<unsafe extern "C" fn(i32, i32, *const c_char, u32) -> i32>;
pub type LayoutSplitWeightedFn =
    Option<unsafe extern "C" fn(i32, i32, i32, i32, u32, *const u32, u32, u32, *mut Rect) -> i32>;

#[repr(C)]
pub struct ApiV2Raw {
    pub size: u32,
    pub abi_version: u32,
    pub log_utf8: LogUtf8Fn,
    pub draw_text: DrawTextFn,
    pub request_permission: RequestPermissionFn,
    pub read_file: ReadFileFn,
    pub free: FreeFn,
}

#[repr(C)]
pub struct ApiV3Raw {
    pub size: u32,
    pub abi_version: u32,
    pub log_utf8: LogUtf8Fn,
    pub draw_text: DrawTextFn,
    pub request_permission: RequestPermissionFn,
    pub read_file: ReadFileFn,
    pub free: FreeFn,
    pub blit_pixels: BlitPixelsFn,
    pub read_file_part: ReadFilePartFn,
}

#[repr(C)]
pub struct ApiV4Raw {
    pub size: u32,
    pub abi_version: u32,
    pub log_utf8: LogUtf8Fn,
    pub draw_text: DrawTextFn,
    pub request_permission: RequestPermissionFn,
    pub read_file: ReadFileFn,
    pub free: FreeFn,
    pub blit_pixels: BlitPixelsFn,
    pub read_file_part: ReadFilePartFn,
    pub draw_text_weighted: DrawTextWeightedFn,
}

#[repr(C)]
pub struct ApiV5Raw {
    pub size: u32,
    pub abi_version: u32,
    pub log_utf8: LogUtf8Fn,
    pub draw_text: DrawTextFn,
    pub request_permission: RequestPermissionFn,
    pub read_file: ReadFileFn,
    pub free: FreeFn,
    pub blit_pixels: BlitPixelsFn,
    pub read_file_part: ReadFilePartFn,
    pub draw_text_weighted: DrawTextWeightedFn,
    pub layout_split_weighted: LayoutSplitWeightedFn,
}

#[derive(Clone, Copy)]
pub struct ApiV2 {
    raw: *const ApiV2Raw,
}

#[derive(Clone, Copy)]
pub struct ApiV3 {
    raw: *const ApiV3Raw,
}

#[derive(Clone, Copy)]
pub struct ApiV4 {
    raw: *const ApiV4Raw,
}

#[derive(Clone, Copy)]
pub struct ApiV5 {
    raw: *const ApiV5Raw,
}

const _: [(); 1] = [(); (core::mem::size_of::<ApiV2Raw>() == 48) as usize];
const _: [(); 1] = [(); (core::mem::size_of::<ApiV3Raw>() == 64) as usize];
const _: [(); 1] = [(); (core::mem::size_of::<ApiV4Raw>() == 72) as usize];
const _: [(); 1] = [(); (core::mem::size_of::<ApiV5Raw>() == 80) as usize];

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Rect {
    pub x: i32,
    pub y: i32,
    pub w: i32,
    pub h: i32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct InputEvent {
    pub kind: u32,
    pub code: u32,
    pub x: i32,
    pub y: i32,
    pub pressed: u32,
}

const _: [(); 1] = [(); (core::mem::size_of::<Rect>() == 16) as usize];
const _: [(); 1] = [(); (core::mem::size_of::<InputEvent>() == 20) as usize];

pub const PERM_UI_DISPLAY: u32 = 1 << 0;
pub const PERM_STORAGE_READ: u32 = 1 << 1;
pub const PERM_STORAGE_WRITE_APP: u32 = 1 << 2;
pub const PERM_INPUT_RECEIVE: u32 = 1 << 3;
pub const PERM_MEDIA_READ: u32 = 1 << 4;

pub const INPUT_KEY: u32 = 1;
pub const INPUT_TOUCH: u32 = 2;
pub const TEXT_WEIGHT_REGULAR: u32 = 0;
pub const TEXT_WEIGHT_BOLD: u32 = 1;
pub const LAYOUT_AXIS_HORIZONTAL: u32 = 0;
pub const LAYOUT_AXIS_VERTICAL: u32 = 1;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ApiError {
    InvalidTable,
    Unsupported,
    Unavailable,
    InvalidArgument,
    InvalidText,
    TooLong,
    Runtime(i32),
}

#[cfg(not(qeapp_combined_crate))]
impl fmt::Display for ApiError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidTable => formatter.write_str("invalid framework API table"),
            Self::Unsupported => formatter.write_str("framework API revision is unsupported"),
            Self::Unavailable => formatter.write_str("framework callback is unavailable"),
            Self::InvalidArgument => formatter.write_str("invalid API argument"),
            Self::InvalidText => formatter.write_str("text must be UTF-8 without NUL"),
            Self::TooLong => formatter.write_str("text exceeds the fixed buffer"),
            Self::Runtime(code) => write!(formatter, "framework API returned {code}"),
        }
    }
}

pub type Result<T> = core::result::Result<T, ApiError>;

pub struct NulString<const N: usize> {
    bytes: core::mem::MaybeUninit<[u8; N]>,
    len: usize,
}

impl<const N: usize> NulString<N> {
    pub fn new() -> Self {
        let mut text = Self {
            bytes: core::mem::MaybeUninit::uninit(),
            len: 0,
        };
        unsafe { text.bytes.as_mut_ptr().cast::<u8>().write(0) };
        text
    }

    pub fn write(&mut self, value: &[u8]) -> Result<()> {
        if N == 0 || value.len() >= N {
            return Err(ApiError::TooLong);
        }
        if !valid_utf8_without_nul(value) {
            return Err(ApiError::InvalidText);
        }
        unsafe {
            let output = self.bytes.as_mut_ptr().cast::<u8>();
            for (index, byte) in value.iter().enumerate() {
                output.add(index).write(*byte);
            }
            output.add(value.len()).write(0);
        }
        self.len = value.len();
        Ok(())
    }

    pub fn as_bytes(&self) -> &[u8] {
        unsafe { core::slice::from_raw_parts(self.bytes.as_ptr().cast::<u8>(), self.len) }
    }

    pub fn as_cstr(&self) -> &CStr {
        unsafe { CStr::from_ptr(self.bytes.as_ptr().cast::<c_char>()) }
    }
}

fn valid_utf8_without_nul(value: &[u8]) -> bool {
    let mut bytes = value.iter();
    while let Some(&first) = bytes.next() {
        match first {
            0 => return false,
            0x01..=0x7f => {}
            0xc2..=0xdf => {
                if !continuation(bytes.next()) {
                    return false;
                }
            }
            0xe0..=0xef => {
                let Some(&second) = bytes.next() else {
                    return false;
                };
                if (first == 0xe0 && !(0xa0..=0xbf).contains(&second))
                    || (first == 0xed && !(0x80..=0x9f).contains(&second))
                    || !continuation(bytes.next())
                {
                    return false;
                }
            }
            0xf0..=0xf4 => {
                let Some(&second) = bytes.next() else {
                    return false;
                };
                if (first == 0xf0 && !(0x90..=0xbf).contains(&second))
                    || (first == 0xf4 && !(0x80..=0x8f).contains(&second))
                    || !continuation(bytes.next())
                    || !continuation(bytes.next())
                {
                    return false;
                }
            }
            _ => return false,
        }
    }
    true
}

fn continuation(value: Option<&u8>) -> bool {
    value.map_or(false, |byte| (0x80..=0xbf).contains(byte))
}

pub fn permission_bit(name: &str) -> u32 {
    if name == "ui.display" {
        PERM_UI_DISPLAY
    } else if name == "storage.read" {
        PERM_STORAGE_READ
    } else if name == "storage.write.app" {
        PERM_STORAGE_WRITE_APP
    } else if name == "input.receive" {
        PERM_INPUT_RECEIVE
    } else if name == "media.read" {
        PERM_MEDIA_READ
    } else {
        0
    }
}

pub struct FileBuffer {
    data: *mut c_void,
    len: usize,
    free: FreeFn,
}

impl FileBuffer {
    fn new(data: *mut c_void, len: usize, free: FreeFn) -> Self {
        Self { data, len, free }
    }

    pub fn as_slice(&self) -> &[u8] {
        if self.len == 0 {
            return &[];
        }
        unsafe { core::slice::from_raw_parts(self.data.cast::<u8>(), self.len) }
    }

    pub fn len(&self) -> usize {
        self.len
    }

    pub fn is_empty(&self) -> bool {
        self.len == 0
    }
}

impl Drop for FileBuffer {
    fn drop(&mut self) {
        if !self.data.is_null() {
            if let Some(free) = self.free {
                unsafe { free(self.data) };
            }
        }
    }
}

pub trait ApiTable: Sized {
    const VERSION: u32;

    unsafe fn from_raw(raw: *const Self) -> Result<Self>;
}

macro_rules! impl_api_deref {
    ($wrapper:ident, $raw:ty) => {
        impl core::ops::Deref for $wrapper {
            type Target = $raw;

            fn deref(&self) -> &Self::Target {
                unsafe { &*self.raw }
            }
        }
    };
}

impl_api_deref!(ApiV2, ApiV2Raw);
impl_api_deref!(ApiV3, ApiV3Raw);
impl_api_deref!(ApiV4, ApiV4Raw);
impl_api_deref!(ApiV5, ApiV5Raw);

macro_rules! impl_common_api {
    ($name:ident, $raw:ty, $version:expr) => {
        impl ApiTable for $name {
            const VERSION: u32 = $version;

            unsafe fn from_raw(raw: *const Self) -> Result<Self> {
                if raw.is_null() {
                    return Err(ApiError::InvalidTable);
                }
                let header = raw.cast::<ApiV2Raw>();
                let size = unsafe { ptr::read_unaligned(header.cast::<u32>()) };
                let abi_version = unsafe { ptr::read_unaligned(header.cast::<u32>().add(1)) };
                if size < core::mem::size_of::<$raw>() as u32 || abi_version != $version {
                    return Err(ApiError::InvalidTable);
                }
                Ok(Self {
                    raw: raw.cast::<$raw>(),
                })
            }
        }

        impl $name {
            pub fn version(&self) -> u32 {
                self.abi_version
            }

            pub unsafe fn log(&self, message: &str) -> Result<()> {
                let mut buffer = NulString::<512>::new();
                buffer.write(message.as_bytes())?;
                self.log_cstr(buffer.as_cstr())
            }

            pub unsafe fn log_cstr(&self, message: &CStr) -> Result<()> {
                let callback = self.log_utf8.ok_or(ApiError::Unavailable)?;
                callback(message.as_ptr() as *const c_char);
                Ok(())
            }

            pub unsafe fn draw_text(&self, x: i32, y: i32, text: &str) -> Result<()> {
                let mut buffer = NulString::<512>::new();
                buffer.write(text.as_bytes())?;
                self.draw_text_cstr(x, y, buffer.as_cstr())
            }

            pub unsafe fn draw_text_cstr(&self, x: i32, y: i32, text: &CStr) -> Result<()> {
                let callback = unsafe { (*self.raw).draw_text }.ok_or(ApiError::Unavailable)?;
                let status = callback(x, y, text.as_ptr() as *const c_char);
                if status == 0 {
                    Ok(())
                } else {
                    Err(ApiError::Runtime(status))
                }
            }

            pub unsafe fn request_permission(&self, permission: &str) -> Result<()> {
                let mut buffer = NulString::<64>::new();
                buffer.write(permission.as_bytes())?;
                self.request_permission_cstr(buffer.as_cstr())
            }

            pub unsafe fn request_permission_cstr(&self, permission: &CStr) -> Result<()> {
                let callback =
                    unsafe { (*self.raw).request_permission }.ok_or(ApiError::Unavailable)?;
                let status = callback(permission.as_ptr() as *const c_char);
                if status == 0 {
                    Ok(())
                } else {
                    Err(ApiError::Runtime(status))
                }
            }

            pub unsafe fn read_file(&self, path: &str) -> Result<FileBuffer> {
                let mut buffer = NulString::<512>::new();
                buffer.write(path.as_bytes())?;
                self.read_file_cstr(buffer.as_cstr())
            }

            pub unsafe fn read_file_cstr(&self, path: &CStr) -> Result<FileBuffer> {
                let callback = unsafe { (*self.raw).read_file }.ok_or(ApiError::Unavailable)?;
                let free = self.free.ok_or(ApiError::Unavailable)?;
                let mut data = ptr::null_mut::<c_void>();
                let mut len = 0usize;
                let status = callback(path.as_ptr() as *const c_char, &mut data, &mut len);
                if status != 0 {
                    if !data.is_null() {
                        free(data);
                    }
                    return Err(ApiError::Runtime(status));
                }
                Ok(FileBuffer::new(data, len, Some(free)))
            }
        }
    };
}

impl_common_api!(ApiV2, ApiV2Raw, 2);
impl_common_api!(ApiV3, ApiV3Raw, 3);
impl_common_api!(ApiV4, ApiV4Raw, 4);
impl_common_api!(ApiV5, ApiV5Raw, 5);

macro_rules! impl_media_api {
    ($name:ident) => {
        impl $name {
            pub unsafe fn blit_pixels(
                &self,
                x: i32,
                y: i32,
                width: i32,
                height: i32,
                stride: i32,
                pixels: &[u16],
            ) -> Result<()> {
                let callback = unsafe { (*self.raw).blit_pixels }.ok_or(ApiError::Unavailable)?;
                let status = callback(x, y, width, height, stride, pixels.as_ptr());
                if status == 0 {
                    Ok(())
                } else {
                    Err(ApiError::Runtime(status))
                }
            }

            pub unsafe fn read_file_part(
                &self,
                path: &str,
                offset: u32,
                out: &mut [u8],
            ) -> Result<()> {
                let mut buffer = NulString::<512>::new();
                buffer.write(path.as_bytes())?;
                let callback =
                    unsafe { (*self.raw).read_file_part }.ok_or(ApiError::Unavailable)?;
                let status = callback(
                    buffer.as_cstr().as_ptr() as *const c_char,
                    offset,
                    out.len(),
                    out.as_mut_ptr().cast::<c_void>(),
                );
                if status == 0 {
                    Ok(())
                } else {
                    Err(ApiError::Runtime(status))
                }
            }
        }
    };
}

impl_media_api!(ApiV3);
impl_media_api!(ApiV4);
impl_media_api!(ApiV5);

impl ApiV4 {
    pub unsafe fn draw_text_weighted(&self, x: i32, y: i32, text: &str, weight: u32) -> Result<()> {
        let mut buffer = NulString::<512>::new();
        buffer.write(text.as_bytes())?;
        let callback = unsafe { (*self.raw).draw_text_weighted }.ok_or(ApiError::Unavailable)?;
        let status = callback(x, y, buffer.as_cstr().as_ptr() as *const c_char, weight);
        if status == 0 {
            Ok(())
        } else {
            Err(ApiError::Runtime(status))
        }
    }
}

impl ApiV5 {
    pub unsafe fn draw_text_weighted(&self, x: i32, y: i32, text: &str, weight: u32) -> Result<()> {
        let mut buffer = NulString::<512>::new();
        buffer.write(text.as_bytes())?;
        let callback = unsafe { (*self.raw).draw_text_weighted }.ok_or(ApiError::Unavailable)?;
        let status = callback(x, y, buffer.as_cstr().as_ptr() as *const c_char, weight);
        if status == 0 {
            Ok(())
        } else {
            Err(ApiError::Runtime(status))
        }
    }

    #[allow(clippy::too_many_arguments)]
    pub unsafe fn layout_split_weighted(
        &self,
        x: i32,
        y: i32,
        width: i32,
        height: i32,
        axis: u32,
        weights: &[u32],
        gap: u32,
        out: &mut [Rect],
    ) -> Result<()> {
        if weights.is_empty() || weights.len() != out.len() || weights.len() > u32::MAX as usize {
            return Err(ApiError::InvalidArgument);
        }
        let callback = unsafe { (*self.raw).layout_split_weighted }.ok_or(ApiError::Unavailable)?;
        let status = callback(
            x,
            y,
            width,
            height,
            axis,
            weights.as_ptr(),
            weights.len() as u32,
            gap,
            out.as_mut_ptr(),
        );
        if status == 0 {
            Ok(())
        } else {
            Err(ApiError::Runtime(status))
        }
    }
}

#[cfg(all(test, not(qeapp_combined_crate)))]
mod tests {
    use super::*;

    static mut FILE_BYTES: [u8; 4] = *b"qeaf";
    static mut FREE_CALLS: usize = 0;

    unsafe extern "C" fn read_test_file(
        _path: *const c_char,
        out: *mut *mut c_void,
        size: *mut usize,
    ) -> i32 {
        unsafe {
            *out = core::ptr::addr_of_mut!(FILE_BYTES).cast();
            *size = 4;
        }
        0
    }

    unsafe extern "C" fn read_failed_file(
        _path: *const c_char,
        out: *mut *mut c_void,
        size: *mut usize,
    ) -> i32 {
        unsafe {
            *out = core::ptr::addr_of_mut!(FILE_BYTES).cast();
            *size = 4;
        }
        -7
    }

    unsafe extern "C" fn free_test_file(data: *mut c_void) {
        if data == core::ptr::addr_of_mut!(FILE_BYTES).cast::<c_void>() {
            unsafe { *core::ptr::addr_of_mut!(FREE_CALLS) += 1 };
        }
    }

    fn raw_api(read_file: ReadFileFn, free: FreeFn) -> ApiV5Raw {
        ApiV5Raw {
            size: core::mem::size_of::<ApiV5Raw>() as u32,
            abi_version: 5,
            log_utf8: None,
            draw_text: None,
            request_permission: None,
            read_file,
            free,
            blit_pixels: None,
            read_file_part: None,
            draw_text_weighted: None,
            layout_split_weighted: None,
        }
    }

    fn reset_free_calls() {
        unsafe { core::ptr::addr_of_mut!(FREE_CALLS).write(0) };
    }

    fn free_calls() -> usize {
        unsafe { core::ptr::addr_of!(FREE_CALLS).read() }
    }

    #[test]
    fn table_size_and_version_are_checked() {
        let mut raw = raw_api(None, None);
        let pointer = (&raw as *const ApiV5Raw).cast::<ApiV5>();
        assert_eq!(unsafe { ApiV5::from_raw(pointer) }.unwrap().version(), 5);
        raw.size -= 1;
        let pointer = (&raw as *const ApiV5Raw).cast::<ApiV5>();
        assert!(matches!(
            unsafe { ApiV5::from_raw(pointer) },
            Err(ApiError::InvalidTable)
        ));
        raw.size = core::mem::size_of::<ApiV5Raw>() as u32;
        raw.abi_version = 4;
        let pointer = (&raw as *const ApiV5Raw).cast::<ApiV5>();
        assert!(matches!(
            unsafe { ApiV5::from_raw(pointer) },
            Err(ApiError::InvalidTable)
        ));
        assert!(matches!(
            unsafe { ApiV5::from_raw(core::ptr::null()) },
            Err(ApiError::InvalidTable)
        ));
    }

    #[test]
    fn nul_string_accepts_utf8_and_rejects_invalid_text() {
        let mut text = NulString::<16>::new();
        assert_eq!(text.write("Xin chào".as_bytes()), Ok(()));
        assert_eq!(text.as_bytes(), "Xin chào".as_bytes());
        assert_eq!(text.as_cstr().to_bytes(), b"Xin ch\xc3\xa0o");
        assert_eq!(text.write(b"bad\0text"), Err(ApiError::InvalidText));
        assert_eq!(text.write(b"\xff"), Err(ApiError::InvalidText));
        assert_eq!(text.write(b"1234567890123456"), Err(ApiError::TooLong));
    }

    #[test]
    fn file_buffer_releases_successful_read_on_drop() {
        reset_free_calls();
        let raw = raw_api(Some(read_test_file), Some(free_test_file));
        let api = unsafe { ApiV5::from_raw((&raw as *const ApiV5Raw).cast::<ApiV5>()).unwrap() };
        let buffer = unsafe { api.read_file("probe.bin") }.unwrap();
        assert_eq!(buffer.len(), 4);
        assert_eq!(buffer.as_slice(), b"qeaf");
        assert_eq!(free_calls(), 0);
        drop(buffer);
        assert_eq!(free_calls(), 1);
    }

    #[test]
    fn failed_read_releases_partial_runtime_buffer() {
        reset_free_calls();
        let raw = raw_api(Some(read_failed_file), Some(free_test_file));
        let api = unsafe { ApiV5::from_raw((&raw as *const ApiV5Raw).cast::<ApiV5>()).unwrap() };
        assert!(matches!(
            unsafe { api.read_file("probe.bin") },
            Err(ApiError::Runtime(-7))
        ));
        assert_eq!(free_calls(), 1);
    }
}
