use std::time::{SystemTime, UNIX_EPOCH};

pub const FLAG_TS_VALID: u8 = 1;
pub const FLAG_SEV_VALID: u8 = 2;

pub const SEV_UNKNOWN: u8 = 0;
pub const SEV_TRACE: u8 = 1;
pub const SEV_DEBUG: u8 = 2;
pub const SEV_INFO: u8 = 3;
pub const SEV_WARN: u8 = 4;
pub const SEV_ERROR: u8 = 5;
pub const SEV_FATAL: u8 = 6;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Meta {
    pub ts: i64,
    pub sev: u8,
    pub flags: u8,
}

impl Meta {
    pub fn unknown() -> Self {
        Self {
            ts: 0,
            sev: SEV_UNKNOWN,
            flags: 0,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ParserKind {
    Nginx,
    Kern,
    Journal,
    Json,
    Logfault,
    Plain,
}

#[derive(Debug, Clone)]
pub struct MetaParser {
    kind: ParserKind,
    base_year: i32,
}

impl MetaParser {
    pub fn new(kind: ParserKind) -> Self {
        Self {
            kind,
            base_year: current_year_utc(),
        }
    }

    pub fn kind(&self) -> ParserKind {
        self.kind
    }

    pub fn parse_line(&self, line: &[u8]) -> Meta {
        match self.kind {
            ParserKind::Nginx => parse_nginx(line),
            ParserKind::Kern => parse_syslog(line, self.base_year, true),
            ParserKind::Journal => parse_syslog(line, self.base_year, false),
            ParserKind::Json => parse_json(line),
            ParserKind::Logfault => parse_logfault(line),
            ParserKind::Plain => parse_plain(line),
        }
    }

    pub fn message_text<'a>(&self, line: &'a [u8]) -> &'a [u8] {
        message_text(self.kind, line)
    }
}

#[derive(Debug, Clone)]
pub struct ParserSelector {
    sample_lines: usize,
}

impl ParserSelector {
    pub fn new(sample_lines: usize) -> Self {
        Self { sample_lines }
    }

    pub fn select<'a, I>(&self, lines: I) -> MetaParser
    where
        I: IntoIterator<Item = &'a [u8]>,
    {
        let mut counts = [0u32; 6];
        let mut seen = 0usize;
        for line in lines.into_iter() {
            if line.is_empty() {
                continue;
            }
            if let Some(kind) = detect_kind(line) {
                counts[kind as usize] += 1;
            }
            seen += 1;
            if seen >= self.sample_lines {
                break;
            }
        }
        let (mut best_idx, mut best_count) = (ParserKind::Plain as usize, 0u32);
        for (idx, count) in counts.iter().enumerate() {
            if *count > best_count {
                best_count = *count;
                best_idx = idx;
            }
        }
        let kind = match best_idx {
            0 => ParserKind::Nginx,
            1 => ParserKind::Kern,
            2 => ParserKind::Journal,
            3 => ParserKind::Json,
            4 => ParserKind::Logfault,
            _ => ParserKind::Plain,
        };
        MetaParser::new(kind)
    }
}

fn detect_kind(line: &[u8]) -> Option<ParserKind> {
    if line.first() == Some(&b'{') && find_subslice(line, b"\"ts\"").is_some() {
        return Some(ParserKind::Json);
    }
    if find_subslice(line, b"HTTP/").is_some() && find_subslice(line, b"[").is_some() {
        return Some(ParserKind::Nginx);
    }
    if find_subslice(line, b"kernel:").is_some() {
        return Some(ParserKind::Kern);
    }
    if find_subslice(line, b"level=").is_some() {
        return Some(ParserKind::Journal);
    }
    if looks_like_logfault(line) {
        return Some(ParserKind::Logfault);
    }
    None
}

fn parse_plain(line: &[u8]) -> Meta {
    let mut meta = Meta::unknown();
    if let Some(ts) = parse_isoish_prefix(line) {
        meta.ts = ts;
        meta.flags |= FLAG_TS_VALID;
    }
    if let Some(sev) = detect_severity(line) {
        meta.sev = sev;
        meta.flags |= FLAG_SEV_VALID;
    }
    meta
}

pub fn message_text<'a>(kind: ParserKind, line: &'a [u8]) -> &'a [u8] {
    match kind {
        ParserKind::Json => extract_json_string(line, b"msg")
            .or_else(|| extract_json_string(line, b"message"))
            .unwrap_or(line),
        ParserKind::Journal => extract_kv_value(line, b"msg").unwrap_or(line),
        ParserKind::Kern => split_after_colon_space(line).unwrap_or(line),
        ParserKind::Logfault => split_logfault_message(line).unwrap_or(line),
        ParserKind::Nginx | ParserKind::Plain => line,
    }
}

fn parse_json(line: &[u8]) -> Meta {
    let mut meta = Meta::unknown();
    if let Some(ts_slice) = extract_json_string(line, b"ts") {
        if let Some(ts) = parse_isoish(ts_slice) {
            meta.ts = ts;
            meta.flags |= FLAG_TS_VALID;
        }
    }
    if let Some(level_slice) = extract_json_string(line, b"level") {
        if let Some(sev) = severity_from_token(level_slice) {
            meta.sev = sev;
            meta.flags |= FLAG_SEV_VALID;
        }
    } else if let Some(sev) = detect_severity(line) {
        meta.sev = sev;
        meta.flags |= FLAG_SEV_VALID;
    }
    meta
}

fn parse_syslog(line: &[u8], base_year: i32, kern_bias: bool) -> Meta {
    let mut meta = Meta::unknown();
    if let Some(ts) = parse_syslog_prefix(line, base_year) {
        meta.ts = ts;
        meta.flags |= FLAG_TS_VALID;
    }
    if let Some(sev) = severity_from_level_token(line) {
        meta.sev = sev;
        meta.flags |= FLAG_SEV_VALID;
        return meta;
    }
    if let Some(sev) = detect_severity(line) {
        meta.sev = if kern_bias && sev == SEV_INFO {
            SEV_DEBUG
        } else {
            sev
        };
        meta.flags |= FLAG_SEV_VALID;
    }
    meta
}

fn parse_nginx(line: &[u8]) -> Meta {
    let mut meta = Meta::unknown();
    if let Some(ts) = parse_nginx_timestamp(line) {
        meta.ts = ts;
        meta.flags |= FLAG_TS_VALID;
    }
    if let Some(code) = parse_nginx_status(line) {
        let sev = if code >= 500 {
            SEV_ERROR
        } else if code >= 400 {
            SEV_WARN
        } else {
            SEV_INFO
        };
        meta.sev = sev;
        meta.flags |= FLAG_SEV_VALID;
    }
    meta
}

fn parse_logfault(line: &[u8]) -> Meta {
    let mut meta = Meta::unknown();
    if let Some((ts, sev)) = parse_logfault_prefix(line) {
        meta.ts = ts;
        meta.sev = sev;
        meta.flags = FLAG_TS_VALID | FLAG_SEV_VALID;
        return meta;
    }
    if let Some(ts) = parse_isoish_prefix(line) {
        meta.ts = ts;
        meta.flags |= FLAG_TS_VALID;
    }
    if let Some(sev) = detect_severity(line) {
        meta.sev = sev;
        meta.flags |= FLAG_SEV_VALID;
    }
    meta
}

fn parse_nginx_timestamp(line: &[u8]) -> Option<i64> {
    let start = find_byte(line, b'[')? + 1;
    let end = find_byte(&line[start..], b']')? + start;
    let slice = &line[start..end];
    if slice.len() < 20 {
        return None;
    }
    let day = parse_two(slice.get(0..2)?)?;
    if slice.get(2)? != &b'/' {
        return None;
    }
    let month = parse_month(slice.get(3..6)?)?;
    if slice.get(6)? != &b'/' {
        return None;
    }
    let year = parse_four(slice.get(7..11)?)?;
    if slice.get(11)? != &b':' {
        return None;
    }
    let hour = parse_two(slice.get(12..14)?)?;
    if slice.get(14)? != &b':' {
        return None;
    }
    let min = parse_two(slice.get(15..17)?)?;
    if slice.get(17)? != &b':' {
        return None;
    }
    let sec = parse_two(slice.get(18..20)?)?;
    let mut idx = 20;
    if idx >= slice.len() || slice.get(idx)? != &b' ' {
        return None;
    }
    idx += 1;
    if idx + 5 > slice.len() {
        return None;
    }
    let sign = *slice.get(idx)?;
    idx += 1;
    let tz_hour = parse_two(slice.get(idx..idx + 2)?)?;
    idx += 2;
    let tz_min = parse_two(slice.get(idx..idx + 2)?)?;
    let offset = (tz_hour as i64) * 3600 + (tz_min as i64) * 60;
    let offset = if sign == b'-' { -offset } else { offset };
    let base = to_epoch_seconds(year, month, day, hour, min, sec)?;
    Some((base - offset) * 1000)
}

fn parse_nginx_status(line: &[u8]) -> Option<u32> {
    let first_quote = find_byte(line, b'"')?;
    let second_quote = find_byte(&line[first_quote + 1..], b'"')? + first_quote + 1;
    let mut idx = second_quote + 1;
    while idx < line.len() && line[idx] == b' ' {
        idx += 1;
    }
    if idx + 3 > line.len() {
        return None;
    }
    let status = parse_three(line.get(idx..idx + 3)?)?;
    Some(status)
}

fn parse_syslog_prefix(line: &[u8], base_year: i32) -> Option<i64> {
    if line.len() < 15 {
        return None;
    }
    let month = parse_month(line.get(0..3)?)?;
    if line.get(3)? != &b' ' {
        return None;
    }
    let (day, idx) = parse_day(&line[4..])?;
    let line = &line[4 + idx..];
    if line.len() < 9 || line.get(0)? != &b' ' {
        return None;
    }
    let hour = parse_two(line.get(1..3)?)?;
    if line.get(3)? != &b':' {
        return None;
    }
    let min = parse_two(line.get(4..6)?)?;
    if line.get(6)? != &b':' {
        return None;
    }
    let sec = parse_two(line.get(7..9)?)?;
    let base = to_epoch_seconds(base_year, month, day, hour, min, sec)?;
    Some(base * 1000)
}

fn parse_isoish_prefix(line: &[u8]) -> Option<i64> {
    if line.len() < 19 {
        return None;
    }
    let slice = &line[..19];
    if !is_digit(slice[0]) {
        return None;
    }
    parse_isoish(line)
}

fn parse_isoish(slice: &[u8]) -> Option<i64> {
    if slice.len() < 19 {
        return None;
    }
    let year = parse_four(slice.get(0..4)?)?;
    if slice.get(4)? != &b'-' {
        return None;
    }
    let month = parse_two(slice.get(5..7)?)?;
    if slice.get(7)? != &b'-' {
        return None;
    }
    let day = parse_two(slice.get(8..10)?)?;
    let sep = slice.get(10)?;
    if *sep != b'T' && *sep != b' ' {
        return None;
    }
    let hour = parse_two(slice.get(11..13)?)?;
    if slice.get(13)? != &b':' {
        return None;
    }
    let min = parse_two(slice.get(14..16)?)?;
    if slice.get(16)? != &b':' {
        return None;
    }
    let sec = parse_two(slice.get(17..19)?)?;
    let mut idx = 19;
    let mut millis = 0i64;
    if slice.get(idx) == Some(&b'.') {
        idx += 1;
        let (ms, consumed) = parse_millis(&slice[idx..])?;
        millis = ms;
        idx += consumed;
    }
    let mut offset = 0i64;
    if let Some(sign) = slice.get(idx) {
        match *sign {
            b'Z' => {}
            b'+' | b'-' => {
                let sign_mult = if *sign == b'-' { -1 } else { 1 };
                idx += 1;
                let (tz_hour, tz_min, _consumed) = parse_tz_offset(&slice[idx..])?;
                offset = sign_mult * ((tz_hour as i64) * 3600 + (tz_min as i64) * 60);
            }
            _ => {}
        }
    }
    let base = to_epoch_seconds(year, month, day, hour, min, sec)? - offset;
    Some(base * 1000 + millis)
}

fn parse_logfault_prefix(line: &[u8]) -> Option<(i64, u8)> {
    if line.len() < 35 || !looks_like_logfault(line) {
        return None;
    }

    let year = parse_four(line.get(0..4)?)?;
    let month = parse_two(line.get(5..7)?)?;
    let day = parse_two(line.get(8..10)?)?;
    let hour = parse_two(line.get(11..13)?)?;
    let min = parse_two(line.get(14..16)?)?;
    let sec = parse_two(line.get(17..19)?)?;
    let millis = parse_three_i64(line.get(20..23)?)?;

    let tz_start = 24;
    let tz_end = scan_token_end(line, tz_start);
    if tz_end <= tz_start || line.get(tz_end) != Some(&b' ') {
        return None;
    }
    let offset = parse_tz_abbrev_offset(line.get(tz_start..tz_end)?)?;

    let level_start = tz_end + 1;
    let level_end = scan_token_end(line, level_start);
    if level_end <= level_start {
        return None;
    }
    let sev = severity_from_token(line.get(level_start..level_end)?)?;

    let base = to_epoch_seconds(year, month, day, hour, min, sec)? - offset;
    Some((base * 1000 + millis, sev))
}

fn parse_tz_offset(slice: &[u8]) -> Option<(u32, u32, usize)> {
    if slice.len() < 2 {
        return None;
    }
    let hour = parse_two(slice.get(0..2)?)?;
    if slice.len() >= 5 && slice.get(2) == Some(&b':') {
        let min = parse_two(slice.get(3..5)?)?;
        return Some((hour, min, 5));
    }
    if slice.len() >= 4 {
        let min = parse_two(slice.get(2..4)?)?;
        return Some((hour, min, 4));
    }
    Some((hour, 0, 2))
}

fn parse_millis(slice: &[u8]) -> Option<(i64, usize)> {
    let mut ms = 0i64;
    let mut count = 0usize;
    for &b in slice.iter().take(3) {
        if !is_digit(b) {
            break;
        }
        ms = ms * 10 + (b - b'0') as i64;
        count += 1;
    }
    if count == 0 {
        return None;
    }
    if count == 1 {
        ms *= 100;
    } else if count == 2 {
        ms *= 10;
    }
    Some((ms, count))
}

fn parse_day(slice: &[u8]) -> Option<(u32, usize)> {
    if slice.len() < 2 {
        return None;
    }
    if slice[0] == b' ' && is_digit(slice[1]) {
        let day = (slice[1] - b'0') as u32;
        return Some((day, 2));
    }
    let day = parse_two(slice.get(0..2)?)?;
    Some((day, 2))
}

fn parse_two(slice: &[u8]) -> Option<u32> {
    if slice.len() != 2 {
        return None;
    }
    if !is_digit(slice[0]) || !is_digit(slice[1]) {
        return None;
    }
    Some(((slice[0] - b'0') as u32) * 10 + (slice[1] - b'0') as u32)
}

fn parse_three(slice: &[u8]) -> Option<u32> {
    if slice.len() != 3 {
        return None;
    }
    if !is_digit(slice[0]) || !is_digit(slice[1]) || !is_digit(slice[2]) {
        return None;
    }
    Some(
        ((slice[0] - b'0') as u32) * 100
            + ((slice[1] - b'0') as u32) * 10
            + (slice[2] - b'0') as u32,
    )
}

fn parse_three_i64(slice: &[u8]) -> Option<i64> {
    parse_three(slice).map(i64::from)
}

fn parse_four(slice: &[u8]) -> Option<i32> {
    if slice.len() != 4 {
        return None;
    }
    if !is_digit(slice[0]) || !is_digit(slice[1]) || !is_digit(slice[2]) || !is_digit(slice[3]) {
        return None;
    }
    let year = ((slice[0] - b'0') as i32) * 1000
        + ((slice[1] - b'0') as i32) * 100
        + ((slice[2] - b'0') as i32) * 10
        + (slice[3] - b'0') as i32;
    Some(year)
}

fn parse_month(slice: &[u8]) -> Option<u32> {
    if slice.len() != 3 {
        return None;
    }
    match slice {
        b"Jan" => Some(1),
        b"Feb" => Some(2),
        b"Mar" => Some(3),
        b"Apr" => Some(4),
        b"May" => Some(5),
        b"Jun" => Some(6),
        b"Jul" => Some(7),
        b"Aug" => Some(8),
        b"Sep" => Some(9),
        b"Oct" => Some(10),
        b"Nov" => Some(11),
        b"Dec" => Some(12),
        _ => None,
    }
}

fn extract_json_string<'a>(line: &'a [u8], key: &[u8]) -> Option<&'a [u8]> {
    let mut idx = 0usize;
    while idx < line.len() {
        let pos = find_subslice(&line[idx..], key)? + idx;
        if pos == 0 || line.get(pos.wrapping_sub(1)) != Some(&b'"') {
            idx = pos + key.len();
            continue;
        }
        let mut i = pos + key.len();
        if line.get(i) != Some(&b'"') {
            idx = i;
            continue;
        }
        i += 1;
        while i < line.len() && line[i] != b':' {
            i += 1;
        }
        if i + 1 >= line.len() {
            return None;
        }
        i += 1;
        while i < line.len() && (line[i] == b' ' || line[i] == b'\t') {
            i += 1;
        }
        if line.get(i) != Some(&b'"') {
            return None;
        }
        i += 1;
        let start = i;
        while i < line.len() && line[i] != b'"' {
            i += 1;
        }
        if i >= line.len() {
            return None;
        }
        return Some(&line[start..i]);
    }
    None
}

fn extract_kv_value<'a>(line: &'a [u8], key: &[u8]) -> Option<&'a [u8]> {
    let mut pattern = Vec::with_capacity(key.len() + 1);
    pattern.extend_from_slice(key);
    pattern.push(b'=');
    let pos = find_subslice(line, &pattern)?;
    let start = pos + pattern.len();
    if start >= line.len() {
        return None;
    }
    if line[start] == b'"' {
        let value_start = start + 1;
        let end = find_byte(&line[value_start..], b'"')? + value_start;
        return Some(&line[value_start..end]);
    }
    let end = scan_token_end(line, start);
    Some(&line[start..end])
}

fn split_after_colon_space(line: &[u8]) -> Option<&[u8]> {
    let pos = find_subslice(line, b": ")?;
    let start = pos + 2;
    (start < line.len()).then_some(&line[start..])
}

fn split_logfault_message(line: &[u8]) -> Option<&[u8]> {
    let mut spaces = 0usize;
    for (idx, byte) in line.iter().enumerate() {
        if *byte == b' ' {
            spaces += 1;
            if spaces == 4 {
                let start = idx + 1;
                return (start < line.len()).then_some(&line[start..]);
            }
        }
    }
    None
}

fn severity_from_level_token(line: &[u8]) -> Option<u8> {
    if let Some(pos) = find_subslice(line, b"level=") {
        let start = pos + 6;
        let end = scan_token_end(line, start);
        return severity_from_token(&line[start..end]);
    }
    if let Some(pos) = find_subslice(line, b"\"level\"") {
        let slice = &line[pos + 7..];
        if let Some(level) = extract_json_string(slice, b"level") {
            return severity_from_token(level);
        }
    }
    None
}

fn detect_severity(line: &[u8]) -> Option<u8> {
    if contains_ci(line, b"ERROR") || contains_ci(line, b"ERR") {
        return Some(SEV_ERROR);
    }
    if contains_ci(line, b"WARN") {
        return Some(SEV_WARN);
    }
    if contains_ci(line, b"FATAL") || contains_ci(line, b"PANIC") || contains_ci(line, b"CRIT") {
        return Some(SEV_FATAL);
    }
    if contains_ci(line, b"TRACE") {
        return Some(SEV_TRACE);
    }
    if contains_ci(line, b"DEBUG") {
        return Some(SEV_DEBUG);
    }
    if contains_ci(line, b"INFO") {
        return Some(SEV_INFO);
    }
    None
}

fn severity_from_token(token: &[u8]) -> Option<u8> {
    if eq_ci(token, b"TRACE") {
        return Some(SEV_TRACE);
    }
    if eq_ci(token, b"DEBUG") {
        return Some(SEV_DEBUG);
    }
    if eq_ci(token, b"INFO") {
        return Some(SEV_INFO);
    }
    if eq_ci(token, b"WARN") || eq_ci(token, b"WARNING") {
        return Some(SEV_WARN);
    }
    if eq_ci(token, b"ERROR") {
        return Some(SEV_ERROR);
    }
    if eq_ci(token, b"FATAL") || eq_ci(token, b"CRIT") {
        return Some(SEV_FATAL);
    }
    None
}

fn scan_token_end(line: &[u8], start: usize) -> usize {
    let mut i = start;
    while i < line.len() {
        let b = line[i];
        if !is_alpha(b) && !is_digit(b) && b != b'_' && b != b'-' {
            break;
        }
        i += 1;
    }
    i
}

fn looks_like_logfault(line: &[u8]) -> bool {
    if line.len() < 35 {
        return false;
    }
    matches!(
        (
            line.get(4),
            line.get(7),
            line.get(10),
            line.get(13),
            line.get(16),
            line.get(19),
            line.get(23)
        ),
        (
            Some(b'-'),
            Some(b'-'),
            Some(b' '),
            Some(b':'),
            Some(b':'),
            Some(b'.'),
            Some(b' ')
        )
    ) && line[..4].iter().all(|b| is_digit(*b))
        && line[5..7].iter().all(|b| is_digit(*b))
        && line[8..10].iter().all(|b| is_digit(*b))
        && line[11..13].iter().all(|b| is_digit(*b))
        && line[14..16].iter().all(|b| is_digit(*b))
        && line[17..19].iter().all(|b| is_digit(*b))
        && line[20..23].iter().all(|b| is_digit(*b))
}

fn parse_tz_abbrev_offset(token: &[u8]) -> Option<i64> {
    let upper = token.iter().map(|b| to_ascii_upper(*b)).collect::<Vec<_>>();
    let hours = match upper.as_slice() {
        b"UTC" | b"GMT" => 0,
        b"CET" => 1,
        b"CEST" => 2,
        b"EET" => 2,
        b"EEST" => 3,
        b"PST" => -8,
        b"PDT" => -7,
        b"MST" => -7,
        b"MDT" => -6,
        b"CST" => -6,
        b"CDT" => -5,
        b"EST" => -5,
        b"EDT" => -4,
        _ => return None,
    };
    Some((hours as i64) * 3600)
}

fn contains_ci(hay: &[u8], needle: &[u8]) -> bool {
    if needle.is_empty() {
        return true;
    }
    if hay.len() < needle.len() {
        return false;
    }
    for i in 0..=hay.len().saturating_sub(needle.len()) {
        if eq_ci(&hay[i..i + needle.len()], needle) {
            return true;
        }
    }
    false
}

fn eq_ci(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() {
        return false;
    }
    for (x, y) in a.iter().zip(b.iter()) {
        if to_ascii_upper(*x) != to_ascii_upper(*y) {
            return false;
        }
    }
    true
}

fn to_ascii_upper(b: u8) -> u8 {
    if b'a' <= b && b <= b'z' {
        b - 32
    } else {
        b
    }
}

fn is_digit(b: u8) -> bool {
    b'0' <= b && b <= b'9'
}

fn is_alpha(b: u8) -> bool {
    (b'A' <= b && b <= b'Z') || (b'a' <= b && b <= b'z')
}

fn find_byte(hay: &[u8], byte: u8) -> Option<usize> {
    hay.iter().position(|&b| b == byte)
}

fn find_subslice(hay: &[u8], needle: &[u8]) -> Option<usize> {
    if needle.is_empty() {
        return Some(0);
    }
    hay.windows(needle.len()).position(|w| w == needle)
}

fn to_epoch_seconds(year: i32, month: u32, day: u32, hour: u32, min: u32, sec: u32) -> Option<i64> {
    if !(1..=12).contains(&month) || !(1..=31).contains(&day) {
        return None;
    }
    let days = days_from_civil(year, month as i32, day as i32);
    let seconds = days * 86_400 + (hour as i64) * 3600 + (min as i64) * 60 + (sec as i64);
    Some(seconds)
}

fn days_from_civil(year: i32, month: i32, day: i32) -> i64 {
    let y = year - if month <= 2 { 1 } else { 0 };
    let era = if y >= 0 { y } else { y - 399 } / 400;
    let yoe = y - era * 400;
    let m = month + if month > 2 { -3 } else { 9 };
    let doy = (153 * m + 2) / 5 + day - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    (era as i64) * 146_097 + (doe as i64) - 719_468
}

fn current_year_utc() -> i32 {
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_else(|_| std::time::Duration::from_secs(0));
    let days = (now.as_secs() as i64) / 86_400;
    let (year, _, _) = civil_from_days(days);
    year
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_isoish_basic() {
        let ts = parse_isoish(b"2026-02-10T12:34:56Z").expect("ts");
        let ts2 = parse_isoish(b"2026-02-10 12:34:56").expect("ts");
        assert_eq!(ts, ts2);
    }

    #[test]
    fn parses_isoish_with_millis_and_offset() {
        let ts = parse_isoish(b"2026-02-10T12:34:56.123+02:00").expect("ts");
        let ts2 = parse_isoish(b"2026-02-10T10:34:56.123Z").expect("ts");
        assert_eq!(ts, ts2);
    }

    #[test]
    fn parses_syslog_time() {
        let year = 2026;
        let ts = parse_syslog_prefix(b"Feb 10 12:34:56 host app", year).expect("ts");
        assert!(ts > 0);
    }

    #[test]
    fn detects_severity_level_tokens() {
        assert_eq!(severity_from_level_token(b"level=INFO").unwrap(), SEV_INFO);
        assert_eq!(
            severity_from_level_token(b"level=ERROR").unwrap(),
            SEV_ERROR
        );
    }

    #[test]
    fn selects_parser_kind() {
        let selector = ParserSelector::new(5);
        let lines = vec![
            b"127.0.0.1 - - [10/Feb/2026:12:34:56 +0000] \"GET / HTTP/1.1\" 200 10 \"-\" \"UA\" 0.1".as_ref(),
            b"127.0.0.1 - - [10/Feb/2026:12:34:56 +0000] \"GET / HTTP/1.1\" 200 10 \"-\" \"UA\" 0.1".as_ref(),
        ];
        let parser = selector.select(lines);
        assert_eq!(parser.kind(), ParserKind::Nginx);
    }

    #[test]
    fn selects_logfault_parser_kind() {
        let selector = ParserSelector::new(5);
        let lines = vec![
            b"2026-03-02 09:20:15.714 EET INFO 13689 nsblast 0.5.2 starting up".as_ref(),
            b"2026-03-02 09:20:15.715 EET TRACE 13689 trace message".as_ref(),
        ];
        let parser = selector.select(lines);
        assert_eq!(parser.kind(), ParserKind::Logfault);
    }

    #[test]
    fn parses_logfault_timestamp_and_severity() {
        let meta = parse_logfault(b"2026-03-02 09:20:15.714 EET TRACE 13689 Beginning transaction");
        assert_eq!(meta.sev, SEV_TRACE);
        assert_eq!(meta.flags & FLAG_TS_VALID, FLAG_TS_VALID);
        assert_eq!(meta.flags & FLAG_SEV_VALID, FLAG_SEV_VALID);
    }
}
