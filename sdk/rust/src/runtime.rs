use core::ffi::{c_char, c_void};

#[no_mangle]
pub unsafe extern "C" fn memcpy(
    destination: *mut c_void,
    source: *const c_void,
    count: usize,
) -> *mut c_void {
    let mut index = 0usize;
    while index < count {
        core::ptr::write_volatile(
            (destination as *mut u8).add(index),
            core::ptr::read_volatile((source as *const u8).add(index)),
        );
        index += 1;
    }
    destination
}

#[no_mangle]
pub unsafe extern "C" fn memset(destination: *mut c_void, value: i32, count: usize) -> *mut c_void {
    let byte = value as u8;
    let mut index = 0usize;
    while index < count {
        core::ptr::write_volatile((destination as *mut u8).add(index), byte);
        index += 1;
    }
    destination
}

#[no_mangle]
pub unsafe extern "C" fn memmove(
    destination: *mut c_void,
    source: *const c_void,
    count: usize,
) -> *mut c_void {
    if (destination as usize) < (source as usize) {
        memcpy(destination, source, count);
    } else {
        let mut index = count;
        while index > 0 {
            index -= 1;
            core::ptr::write_volatile(
                (destination as *mut u8).add(index),
                core::ptr::read_volatile((source as *const u8).add(index)),
            );
        }
    }
    destination
}

#[no_mangle]
pub unsafe extern "C" fn memcmp(left: *const c_void, right: *const c_void, count: usize) -> i32 {
    let mut index = 0usize;
    while index < count {
        let a = core::ptr::read_volatile((left as *const u8).add(index));
        let b = core::ptr::read_volatile((right as *const u8).add(index));
        if a != b {
            return a as i32 - b as i32;
        }
        index += 1;
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn strlen(source: *const c_char) -> usize {
    let mut length = 0usize;
    loop {
        let byte = core::ptr::read_volatile(source.add(length));
        if byte == 0 {
            return length;
        }
        length += 1;
    }
}
