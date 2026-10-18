use std::io::Error;
use std::io::Write;
use std::net::TcpStream;

pub(crate) fn open_connection() -> Result<TcpStream, Error> {
    match TcpStream::connect("10.10.10.2:5000") {
        Ok(stream) => Ok(stream),
        Err(e) => {
            eprintln!("TCP connection failed: {}", e);
            Err(e)
        }
    }
}

pub(crate) fn start_test(stream: &mut TcpStream, test_name: &str) -> Result<(), Error> {
    let command = format!("start {}\n", test_name);
    stream.write_all(command.as_bytes())
}
pub(crate) fn test_complete(stream: &mut TcpStream) -> Result<(), Error> {
    stream.write_all(b"complete\n")
}
