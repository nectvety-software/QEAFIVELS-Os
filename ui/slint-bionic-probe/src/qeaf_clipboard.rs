//! QEAF clipboard IPC client. The service endpoint is intentionally explicit and
//! does not fall back to process-local static storage.
use std::io::{Read, Write};
use std::os::unix::net::UnixStream;
use std::time::Duration;

const SOCKET: &str = "/run/qeaf/clipboard.sock";
const MAX_BYTES: usize = 4096;

fn connect() -> std::io::Result<UnixStream> {
    let stream = UnixStream::connect(SOCKET)?;
    stream.set_read_timeout(Some(Duration::from_millis(150)))?;
    stream.set_write_timeout(Some(Duration::from_millis(150)))?;
    Ok(stream)
}

pub fn set(text: &str) -> std::io::Result<()> {
    if text.len() > MAX_BYTES || text.as_bytes().contains(&0) { return Err(std::io::Error::new(std::io::ErrorKind::InvalidInput, "clipboard text exceeds QEAF limit or contains NUL")); }
    let mut s = connect()?;
    write!(s, "SET {}\n", text.len())?;
    s.write_all(text.as_bytes())?;
    s.flush()?;
    let mut ack = [0u8; 3];
    s.read_exact(&mut ack)?;
    if &ack != b"OK\n" { return Err(std::io::Error::new(std::io::ErrorKind::Other, "QEAF clipboard service rejected SET")); }
    Ok(())
}

pub fn get() -> std::io::Result<Option<String>> {
    let mut s = connect()?;
    s.write_all(b"GET\n")?;
    let mut header = Vec::with_capacity(16);
    loop {
        let mut b = [0u8; 1]; s.read_exact(&mut b)?;
        if b[0] == b'\n' { break; }
        if header.len() >= 16 { return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "clipboard header too long")); }
        header.push(b[0]);
    }
    if header == b"EMPTY" { return Ok(None); }
    let n: usize = std::str::from_utf8(&header).ok().and_then(|v| v.strip_prefix("DATA ")).and_then(|v| v.parse().ok()).filter(|n| *n <= MAX_BYTES).ok_or_else(|| std::io::Error::new(std::io::ErrorKind::InvalidData, "invalid clipboard response"))?;
    let mut bytes = vec![0; n]; s.read_exact(&mut bytes)?;
    let text = String::from_utf8(bytes).map_err(|_| std::io::Error::new(std::io::ErrorKind::InvalidData, "clipboard response is not UTF-8"))?;
    Ok(Some(text))
}
