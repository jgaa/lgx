use std::env;
use std::fs::{self, File};
use std::io::{self, BufWriter, Write};
use std::path::PathBuf;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

const DEFAULT_LPS: u32 = 1000;
const DEFAULT_TICK_MS: u64 = 20;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Format {
    Nginx,
    Kern,
    Journal,
    Json,
    Plain,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum FlushMode {
    None,
    Line,
    Interval(u64),
}

#[derive(Debug, Clone)]
struct Config {
    lps: u32,
    format: Format,
    out: Option<PathBuf>,
    stdout: bool,
    append: Option<bool>,
    seed: u64,
    duration_secs: Option<u64>,
    burst: Option<u32>,
    flush: FlushMode,
    no_newline_rate: f64,
}

fn main() {
    let args: Vec<String> = env::args().collect();
    let config = match parse_args(&args) {
        Ok(cfg) => cfg,
        Err(err) => {
            eprintln!("{err}\n");
            eprintln!("{}", usage());
            std::process::exit(2);
        }
    };

    if config.lps == 0 {
        eprintln!("--lps must be > 0");
        std::process::exit(2);
    }

    if config.stdout && config.out.is_some() {
        eprintln!("--stdout and --out cannot be used together");
        std::process::exit(2);
    }

    let output = match open_output(&config) {
        Ok(out) => out,
        Err(err) => {
            eprintln!("failed to open output: {err}");
            std::process::exit(1);
        }
    };

    if let Err(err) = run(config, output) {
        eprintln!("error: {err}");
        std::process::exit(1);
    }
}

fn usage() -> &'static str {
    "logsim --lps <lines/sec> --format <nginx|kern|journal|json|plain> [--out <path>|--stdout] [--append] [--seed <u64>] [--duration <secs>] [--burst <u32>] [--flush <none|line|interval:ms>] [--no-newline-rate <p>]"
}

fn parse_args(args: &[String]) -> Result<Config, String> {
    let mut lps = DEFAULT_LPS;
    let mut format = Format::Plain;
    let mut out = None;
    let mut stdout = false;
    let mut append = None;
    let mut seed = default_seed();
    let mut duration_secs = None;
    let mut burst = None;
    let mut flush = FlushMode::None;
    let mut no_newline_rate = 0.0f64;

    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "--help" | "-h" => {
                return Err("help requested".to_string());
            }
            "--lps" => {
                let val = next_value(args, &mut i, "--lps")?;
                lps = val
                    .parse::<u32>()
                    .map_err(|_| "--lps must be u32".to_string())?;
            }
            "--format" => {
                let val = next_value(args, &mut i, "--format")?;
                format = parse_format(&val)?;
            }
            "--out" => {
                let val = next_value(args, &mut i, "--out")?;
                out = Some(PathBuf::from(val));
            }
            "--stdout" => {
                stdout = true;
            }
            "--append" => {
                append = Some(true);
            }
            "--seed" => {
                let val = next_value(args, &mut i, "--seed")?;
                seed = val
                    .parse::<u64>()
                    .map_err(|_| "--seed must be u64".to_string())?;
            }
            "--duration" => {
                let val = next_value(args, &mut i, "--duration")?;
                duration_secs = Some(
                    val.parse::<u64>()
                        .map_err(|_| "--duration must be u64".to_string())?,
                );
            }
            "--burst" => {
                let val = next_value(args, &mut i, "--burst")?;
                let burst_val = val
                    .parse::<u32>()
                    .map_err(|_| "--burst must be u32".to_string())?;
                if burst_val > 0 {
                    burst = Some(burst_val);
                }
            }
            "--flush" => {
                let val = next_value(args, &mut i, "--flush")?;
                flush = parse_flush(&val)?;
            }
            "--no-newline-rate" => {
                let val = next_value(args, &mut i, "--no-newline-rate")?;
                no_newline_rate = val
                    .parse::<f64>()
                    .map_err(|_| "--no-newline-rate must be f64".to_string())?;
                if !(0.0..=1.0).contains(&no_newline_rate) {
                    return Err("--no-newline-rate must be between 0 and 1".to_string());
                }
            }
            "--rotate" => {
                return Err("--rotate is not implemented".to_string());
            }
            other => {
                return Err(format!("unknown argument: {other}"));
            }
        }
        i += 1;
    }

    Ok(Config {
        lps,
        format,
        out,
        stdout,
        append,
        seed,
        duration_secs,
        burst,
        flush,
        no_newline_rate,
    })
}

fn next_value(args: &[String], i: &mut usize, flag: &str) -> Result<String, String> {
    if *i + 1 >= args.len() {
        return Err(format!("{flag} expects a value"));
    }
    *i += 1;
    Ok(args[*i].clone())
}

fn parse_format(val: &str) -> Result<Format, String> {
    match val {
        "nginx" => Ok(Format::Nginx),
        "kern" => Ok(Format::Kern),
        "journal" => Ok(Format::Journal),
        "json" => Ok(Format::Json),
        "plain" => Ok(Format::Plain),
        _ => Err("--format must be one of nginx|kern|journal|json|plain".to_string()),
    }
}

fn parse_flush(val: &str) -> Result<FlushMode, String> {
    if val == "none" {
        return Ok(FlushMode::None);
    }
    if val == "line" {
        return Ok(FlushMode::Line);
    }
    if let Some(ms) = val.strip_prefix("interval:") {
        let ms = ms
            .parse::<u64>()
            .map_err(|_| "--flush interval must be u64".to_string())?;
        return Ok(FlushMode::Interval(ms));
    }
    Err("--flush must be none|line|interval:<ms>".to_string())
}

fn default_seed() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_nanos() as u64)
        .unwrap_or(0x1234_5678_9abc_def0)
}

enum Output {
    Stdout(BufWriter<io::Stdout>),
    File(BufWriter<File>),
}

impl Output {
    fn write_all(&mut self, buf: &[u8]) -> io::Result<()> {
        match self {
            Output::Stdout(w) => w.write_all(buf),
            Output::File(w) => w.write_all(buf),
        }
    }

    fn flush(&mut self) -> io::Result<()> {
        match self {
            Output::Stdout(w) => w.flush(),
            Output::File(w) => w.flush(),
        }
    }

    fn sync_if_file(&mut self) -> io::Result<()> {
        match self {
            Output::Stdout(_) => Ok(()),
            Output::File(w) => w.get_ref().sync_data(),
        }
    }
}

fn open_output(config: &Config) -> io::Result<Output> {
    if config.stdout || config.out.is_none() {
        let out = io::stdout();
        return Ok(Output::Stdout(BufWriter::new(out)));
    }

    let path = config.out.as_ref().expect("out path");
    let exists = path.exists();
    let append = config.append.unwrap_or(exists);

    let mut options = fs::OpenOptions::new();
    options.create(true).write(true);
    if append {
        options.append(true);
    } else {
        options.truncate(true);
    }

    let file = options.open(path)?;
    Ok(Output::File(BufWriter::new(file)))
}

fn run(config: Config, mut output: Output) -> io::Result<()> {
    let mut rng = Rng::new(config.seed);
    let start_time = Instant::now();
    let mut last_tick = Instant::now();
    let mut last_flush = Instant::now();
    let mut credit = 0.0f64;
    let tick = Duration::from_millis(DEFAULT_TICK_MS);
    let mut pending_no_newline = false;

    loop {
        if let Some(limit) = config.duration_secs {
            if start_time.elapsed() >= Duration::from_secs(limit) {
                break;
            }
        }

        let emit_count = if let Some(burst) = config.burst {
            burst
        } else {
            let now = Instant::now();
            let dt = now.duration_since(last_tick);
            last_tick = now;
            credit += config.lps as f64 * dt.as_secs_f64();
            let emit = credit.floor() as u32;
            credit -= emit as f64;
            emit
        };

        if emit_count == 0 {
            sleep_tick(tick);
            continue;
        }

        for _ in 0..emit_count {
            if pending_no_newline {
                output.write_all(b"\n")?;
                if matches!(config.flush, FlushMode::Line) {
                    output.flush()?;
                    output.sync_if_file()?;
                }
                pending_no_newline = false;
            }

            let ts = Timestamp::now();
            let line = generate_line(config.format, &mut rng, &ts);
            let omit_newline = rng.next_f64() < config.no_newline_rate;
            output.write_all(line.as_bytes())?;
            if omit_newline {
                pending_no_newline = true;
            } else {
                output.write_all(b"\n")?;
                if matches!(config.flush, FlushMode::Line) {
                    output.flush()?;
                    output.sync_if_file()?;
                }
            }
        }

        match config.flush {
            FlushMode::None => {}
            FlushMode::Line => {}
            FlushMode::Interval(ms) => {
                if last_flush.elapsed() >= Duration::from_millis(ms) {
                    output.flush()?;
                    output.sync_if_file()?;
                    last_flush = Instant::now();
                }
            }
        }

        if let Some(burst) = config.burst {
            let interval = burst_interval(config.lps, burst);
            sleep_tick(interval);
        } else {
            sleep_tick(tick);
        }
    }

    output.flush()?;
    output.sync_if_file()?;
    Ok(())
}

fn sleep_tick(d: Duration) {
    if d.as_millis() == 0 {
        return;
    }
    std::thread::sleep(d);
}

fn burst_interval(lps: u32, burst: u32) -> Duration {
    let lps = lps.max(1) as f64;
    let burst = burst.max(1) as f64;
    let secs = burst / lps;
    Duration::from_secs_f64(secs.max(0.001))
}

fn generate_line(format: Format, rng: &mut Rng, ts: &Timestamp) -> String {
    match format {
        Format::Nginx => nginx_line(rng, ts),
        Format::Kern => kern_line(rng, ts),
        Format::Journal => journal_line(rng, ts),
        Format::Json => json_line(rng, ts),
        Format::Plain => plain_line(rng, ts),
    }
}

fn nginx_line(rng: &mut Rng, ts: &Timestamp) -> String {
    let ip = format!(
        "{}.{}.{}.{}",
        rng.range_u32(1, 223),
        rng.range_u32(0, 255),
        rng.range_u32(0, 255),
        rng.range_u32(1, 254)
    );
    let method = pick(rng, &["GET", "POST", "PUT", "DELETE", "PATCH", "HEAD"]);
    let path = pick(
        rng,
        &[
            "/",
            "/api/v1/items",
            "/api/v1/items/42",
            "/login",
            "/logout",
            "/health",
            "/static/app.js",
        ],
    );
    let query = if rng.next_u32() % 3 == 0 {
        format!("?id={}", rng.range_u32(1, 5000))
    } else {
        "".to_string()
    };
    let status = pick(
        rng,
        &[
            "200", "201", "204", "301", "302", "400", "401", "403", "404", "500", "502", "503",
        ],
    );
    let size = rng.range_u32(128, 9000);
    let ua = pick(
        rng,
        &[
            "Mozilla/5.0",
            "curl/8.0",
            "k6/0.45",
            "PostmanRuntime/7.36",
            "Go-http-client/1.1",
        ],
    );
    let req_time = (rng.next_u32() % 900) as f64 / 1000.0 + 0.001;
    format!(
        "{ip} - - [{}] \"{method} {path}{query} HTTP/1.1\" {status} {size} \"-\" \"{ua}\" {:.3}",
        ts.nginx_time(),
        req_time
    )
}

fn kern_line(rng: &mut Rng, ts: &Timestamp) -> String {
    let host = pick(rng, &["host", "edge", "node", "router", "gw"]);
    let dev = pick(rng, &["eth0", "eth1", "wlan0", "enp0s3"]);
    let speed = pick(rng, &["100Mbps", "1000Mbps", "2500Mbps"]);
    let msg = pick(
        rng,
        &[
            "link up, full-duplex",
            "link down",
            "NIC reset",
            "TX timeout, resetting",
            "entered promiscuous mode",
        ],
    );
    let uptime = rng.next_u32() as f64 / 100.0;
    format!(
        "{} {host} kernel: [{uptime:.6}] {dev}: {msg}, {speed}",
        ts.syslog_time(),
    )
}

fn journal_line(rng: &mut Rng, ts: &Timestamp) -> String {
    let host = pick(rng, &["host", "edge", "node", "db", "api"]);
    let app = pick(rng, &["app", "worker", "scheduler", "ingest", "proxy"]);
    let pid = rng.range_u32(100, 5000);
    let level = pick(rng, &["INFO", "WARN", "ERROR", "DEBUG"]);
    let msg = pick(
        rng,
        &[
            "processed request",
            "connected to upstream",
            "cache miss",
            "retrying after error",
            "task completed",
        ],
    );
    let req = rng.hex(12);
    let user = pick(rng, &["alice", "bob", "carol", "dave", "eve"]);
    format!(
        "{} {host} {app}[{pid}]: level={level} msg=\"{msg}\" req_id={req} user={user}",
        ts.syslog_time()
    )
}

fn json_line(rng: &mut Rng, ts: &Timestamp) -> String {
    let level = pick(rng, &["INFO", "WARN", "ERROR", "DEBUG"]);
    let msg = pick(
        rng,
        &[
            "db slow",
            "cache hit",
            "request failed",
            "queue depth high",
            "reaped worker",
        ],
    );
    let ms = rng.range_u32(1, 5000);
    let req = rng.hex(16);
    format!(
        "{{\"ts\":\"{}\",\"level\":\"{level}\",\"msg\":\"{msg}\",\"ms\":{ms},\"req\":\"{req}\"}}",
        ts.rfc3339()
    )
}

fn plain_line(rng: &mut Rng, ts: &Timestamp) -> String {
    let msg = pick(
        rng,
        &[
            "worker started",
            "connected",
            "heartbeat",
            "processing batch",
            "completed",
        ],
    );
    format!("{} {msg}", ts.simple_time())
}

fn pick<'a>(rng: &mut Rng, items: &'a [&'a str]) -> &'a str {
    let idx = (rng.next_u32() as usize) % items.len();
    items[idx]
}

struct Timestamp {
    year: i32,
    month: u32,
    day: u32,
    hour: u32,
    min: u32,
    sec: u32,
    millis: u32,
}

impl Timestamp {
    fn now() -> Self {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_else(|_| Duration::from_secs(0));
        let secs = now.as_secs() as i64;
        let millis = now.subsec_millis();
        let (year, month, day, hour, min, sec) = unix_to_parts(secs);
        Self {
            year,
            month,
            day,
            hour,
            min,
            sec,
            millis,
        }
    }

    fn syslog_time(&self) -> String {
        format!(
            "{} {:02} {:02}:{:02}:{:02}",
            month_name(self.month),
            self.day,
            self.hour,
            self.min,
            self.sec
        )
    }

    fn nginx_time(&self) -> String {
        format!(
            "{:02}/{}/{}:{:02}:{:02}:{:02} +0000",
            self.day,
            month_name(self.month),
            self.year,
            self.hour,
            self.min,
            self.sec
        )
    }

    fn rfc3339(&self) -> String {
        format!(
            "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z",
            self.year, self.month, self.day, self.hour, self.min, self.sec, self.millis
        )
    }

    fn simple_time(&self) -> String {
        format!(
            "{:04}-{:02}-{:02} {:02}:{:02}:{:02}",
            self.year, self.month, self.day, self.hour, self.min, self.sec
        )
    }
}

fn month_name(month: u32) -> &'static str {
    match month {
        1 => "Jan",
        2 => "Feb",
        3 => "Mar",
        4 => "Apr",
        5 => "May",
        6 => "Jun",
        7 => "Jul",
        8 => "Aug",
        9 => "Sep",
        10 => "Oct",
        11 => "Nov",
        12 => "Dec",
        _ => "Jan",
    }
}

fn unix_to_parts(secs: i64) -> (i32, u32, u32, u32, u32, u32) {
    let days = secs.div_euclid(86_400);
    let rem = secs.rem_euclid(86_400);
    let hour = (rem / 3600) as u32;
    let min = ((rem % 3600) / 60) as u32;
    let sec = (rem % 60) as u32;
    let (year, month, day) = civil_from_days(days);
    (year, month, day, hour, min, sec)
}

fn civil_from_days(days: i64) -> (i32, u32, u32) {
    let z = days + 719_468;
    let era = if z >= 0 { z } else { z - 146_096 } / 146_097;
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = mp + if mp < 10 { 3 } else { -9 };
    let year = y + if m <= 2 { 1 } else { 0 };
    (year as i32, m as u32, d as u32)
}

struct Rng {
    state: u64,
}

impl Rng {
    fn new(seed: u64) -> Self {
        let seed = if seed == 0 {
            0x9e37_79b9_7f4a_7c15
        } else {
            seed
        };
        Self { state: seed }
    }

    fn next_u64(&mut self) -> u64 {
        let mut x = self.state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        self.state = x;
        x.wrapping_mul(0x2545F4914F6CDD1D)
    }

    fn next_u32(&mut self) -> u32 {
        (self.next_u64() >> 32) as u32
    }

    fn next_f64(&mut self) -> f64 {
        let v = (self.next_u64() >> 11) as u64;
        (v as f64) / ((1u64 << 53) as f64)
    }

    fn range_u32(&mut self, min: u32, max: u32) -> u32 {
        if min >= max {
            return min;
        }
        let span = max - min + 1;
        min + (self.next_u32() % span)
    }

    fn hex(&mut self, len: usize) -> String {
        let mut out = String::with_capacity(len);
        for _ in 0..len {
            let v = (self.next_u32() % 16) as u8;
            out.push(nibble_to_hex(v));
        }
        out
    }
}

fn nibble_to_hex(v: u8) -> char {
    match v {
        0..=9 => (b'0' + v) as char,
        10..=15 => (b'a' + (v - 10)) as char,
        _ => '0',
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_args_basic() {
        let args = vec![
            "logsim".to_string(),
            "--lps".to_string(),
            "2000".to_string(),
            "--format".to_string(),
            "nginx".to_string(),
            "--out".to_string(),
            "/tmp/lgx.log".to_string(),
            "--append".to_string(),
            "--seed".to_string(),
            "42".to_string(),
            "--duration".to_string(),
            "5".to_string(),
            "--burst".to_string(),
            "100".to_string(),
            "--flush".to_string(),
            "interval:200".to_string(),
            "--no-newline-rate".to_string(),
            "0.05".to_string(),
        ];

        let cfg = parse_args(&args).expect("parse args");
        assert_eq!(cfg.lps, 2000);
        assert_eq!(cfg.format, Format::Nginx);
        assert_eq!(cfg.out, Some(PathBuf::from("/tmp/lgx.log")));
        assert_eq!(cfg.append, Some(true));
        assert_eq!(cfg.seed, 42);
        assert_eq!(cfg.duration_secs, Some(5));
        assert_eq!(cfg.burst, Some(100));
        assert_eq!(cfg.flush, FlushMode::Interval(200));
        assert!((cfg.no_newline_rate - 0.05).abs() < 1e-9);
    }

    #[test]
    fn parse_flush_modes() {
        assert_eq!(parse_flush("none").unwrap(), FlushMode::None);
        assert_eq!(parse_flush("line").unwrap(), FlushMode::Line);
        assert_eq!(
            parse_flush("interval:150").unwrap(),
            FlushMode::Interval(150)
        );
        assert!(parse_flush("interval:bad").is_err());
    }

    #[test]
    fn parse_format_values() {
        assert_eq!(parse_format("json").unwrap(), Format::Json);
        assert!(parse_format("nope").is_err());
    }
}
