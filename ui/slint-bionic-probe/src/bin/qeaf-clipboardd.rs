use std::io::{Read, Write};
use std::os::unix::fs::PermissionsExt;
use std::os::unix::net::{UnixListener, UnixStream};
use std::path::Path;
use std::sync::{Arc, Mutex};

const MAX: usize = 4096;
fn main() -> std::io::Result<()> {
    let path = std::env::var("QEAF_CLIPBOARD_SOCKET").unwrap_or_else(|_| "/run/qeaf/clipboard.sock".into());
    if let Some(parent) = Path::new(&path).parent() { std::fs::create_dir_all(parent)?; }
    let _ = std::fs::remove_file(&path);
    let listener = UnixListener::bind(&path)?;
    std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o660))?;
    if let Ok(gid) = std::env::var("QEAF_CLIPBOARD_GID") {
        let gid: libc::gid_t = gid.parse().map_err(|_| std::io::Error::new(std::io::ErrorKind::InvalidInput, "invalid QEAF_CLIPBOARD_GID"))?;
        use std::os::unix::ffi::OsStrExt;
        let cpath = std::ffi::CString::new(Path::new(&path).as_os_str().as_bytes())
            .map_err(|_| std::io::Error::new(std::io::ErrorKind::InvalidInput, "socket path contains NUL"))?;
        if unsafe { libc::chown(cpath.as_ptr(), u32::MAX, gid) } != 0 {
            return Err(std::io::Error::last_os_error());
        }
    }
    eprintln!("qeaf-clipboardd: listening at {path}");
    let state = Arc::new(Mutex::new(String::new()));
    for stream in listener.incoming() {
        match stream {
            Ok(s) => {
                let shared = state.clone();
                std::thread::spawn(move || {
                    if let Err(e) = serve(s, shared) { eprintln!("qeaf-clipboardd: client: {e}"); }
                });
            }
            Err(e) => eprintln!("qeaf-clipboardd: accept: {e}"),
        }
    }
    Ok(())
}
fn serve(mut s: UnixStream, state: Arc<Mutex<String>>) -> std::io::Result<()> {
    s.set_read_timeout(Some(std::time::Duration::from_secs(2)))?;
    s.set_write_timeout(Some(std::time::Duration::from_secs(2)))?;
    let mut line = Vec::with_capacity(32);
    loop {
        let mut b = [0u8;1]; s.read_exact(&mut b)?;
        if b[0] == b'\n' { break; }
        if line.len() >= 32 { return Err(std::io::Error::new(std::io::ErrorKind::InvalidData,"request header too long")); }
        line.push(b[0]);
    }
    if line == b"GET" {
        let value = state.lock().unwrap().clone();
        if value.is_empty() { s.write_all(b"EMPTY\n")?; }
        else { write!(s,"DATA {}\n",value.len())?; s.write_all(value.as_bytes())?; }
        return Ok(());
    }
    let len: usize = std::str::from_utf8(&line).ok()
        .and_then(|v| v.strip_prefix("SET ")).and_then(|v| v.parse().ok())
        .filter(|n| *n <= MAX).ok_or_else(|| std::io::Error::new(std::io::ErrorKind::InvalidData,"invalid SET header"))?;
    let mut bytes = vec![0u8;len]; s.read_exact(&mut bytes)?;
    let value = String::from_utf8(bytes).map_err(|_| std::io::Error::new(std::io::ErrorKind::InvalidData,"clipboard must be UTF-8"))?;
    if value.as_bytes().contains(&0) { return Err(std::io::Error::new(std::io::ErrorKind::InvalidData,"NUL is forbidden")); }
    *state.lock().unwrap() = value;
    s.write_all(b"OK\n")
}

#[cfg(test)]
mod tests {
    use super::*;
    fn exchange(request: &[u8], state: Arc<Mutex<String>>) -> std::io::Result<Vec<u8>> {
        let (mut client, server) = UnixStream::pair()?;
        let worker = std::thread::spawn(move || serve(server, state));
        client.write_all(request)?;
        let mut response = Vec::new();
        client.read_to_end(&mut response)?;
        worker.join().unwrap()?;
        Ok(response)
    }
    #[test]
    fn empty_and_utf8_round_trip() {
        let state = Arc::new(Mutex::new(String::new()));
        assert_eq!(exchange(b"GET\n", state.clone()).unwrap(), b"EMPTY\n");
        let value = "QEAF clipboard tiếng Việt ✓";
        let body = value.as_bytes();
        let mut request = format!("SET {}\n", body.len()).into_bytes();
        request.extend_from_slice(body);
        assert_eq!(exchange(&request, state.clone()).unwrap(), b"OK\n");
        let mut expected = format!("DATA {}\n", body.len()).into_bytes();
        expected.extend_from_slice(body);
        assert_eq!(exchange(b"GET\n", state).unwrap(), expected);
    }
    #[test]
    fn rejects_oversized_payload_header() {
        let state = Arc::new(Mutex::new(String::new()));
        assert!(exchange(b"SET 4097\n", state).is_err());
    }
}
