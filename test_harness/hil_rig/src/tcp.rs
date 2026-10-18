use std::io::Write;
use std::net::TcpStream;
use std::io::Error;


pub(crate) fn open_connection() -> Result<TcpStream, Error> {
    match TcpStream::connect("192.168.1.100:5000") {
        Ok(stream) => Ok(stream),
        Err(e) => {
            eprintln!("TCP connection failed: {}", e);
            Err(e)
        }
    }
}

pub(crate) fn start_test(stream: &mut TcpStream, test_name: &str){
    let command = format!("start {}\n", test_name);
    match stream.write_all(command.as_bytes()){
        Ok(_) => return,
        Err(e) => println!("TCP write failed: {}",e)
    }
}
pub(crate) fn test_complete(stream: &mut TcpStream){
    match stream.write_all(b"complete\n"){
        Ok(_) => return,
        Err(e) => println!("TCP write failed: {}",e)
    }
}
