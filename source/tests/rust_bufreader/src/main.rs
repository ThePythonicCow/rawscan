use std::io::prelude::*;
use std::io::BufReader;
use std::fs::File;
use std::os::unix::io::FromRawFd;

fn main() -> std::io::Result<()> {
    let input = unsafe { File::from_raw_fd(0) };
    let buffered = BufReader::new(input);

    for line in buffered.lines() {
        match line {
            Ok(line) => {
                if line.starts_with("abc") {
                    println!("{}", line);
                }
            },
            Err(e) => return Err(e),
        }
    }

    Ok(())
}
