// Following Rust code adapted from an example at:
//
//      https://crates.io/crates/bstr
//
// I replaced "contains_str" with "starts_with", and
// replaced the search pattern "Dimension" with the
// search pattern "abc", as used in other tests here.
//
// Paul Jackson
// pj@usa.net
// 24 Nov 2019

use std::error::Error;
use std::io::{self, Write};

use bstr::io::BufReadExt;

fn main() -> Result<(), Box<dyn Error>> {
    let stdin = io::stdin();
    let mut stdout = io::BufWriter::new(io::stdout());

    stdin.lock().for_byte_line_with_terminator(|line| {
        if line.starts_with(b"abc") {
            stdout.write_all(line)?;
        }
        Ok(true)
    })?;
    Ok(())
}
