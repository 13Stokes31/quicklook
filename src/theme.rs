//! Fondo de ventana de KDE, leído al abrir una vista previa.

use std::path::PathBuf;

const FALLBACK_BACKGROUND: u32 = 0xffffff;

pub fn window_background() -> u32 {
    let config_home = std::env::var_os("XDG_CONFIG_HOME")
        .filter(|path| !path.is_empty())
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|home| PathBuf::from(home).join(".config")));
    config_home
        .and_then(|path| std::fs::read_to_string(path.join("kdeglobals")).ok())
        .and_then(|config| parse_window_background(&config))
        .unwrap_or(FALLBACK_BACKGROUND)
}

fn parse_rgb(value: &str) -> Option<u32> {
    let mut channels = value.split(',');
    let red = channels.next()?.trim().parse::<u8>().ok()?;
    let green = channels.next()?.trim().parse::<u8>().ok()?;
    let blue = channels.next()?.trim().parse::<u8>().ok()?;
    if channels.next().is_some() {
        return None;
    }
    Some((u32::from(red) << 16) | (u32::from(green) << 8) | u32::from(blue))
}

fn parse_window_background(config: &str) -> Option<u32> {
    let mut in_window = false;
    let mut color = None;
    for line in config.lines().map(str::trim) {
        if line.starts_with('[') {
            in_window = line == "[Colors:Window]";
        } else if in_window {
            if let Some((key, value)) = line.split_once('=') {
                if key.trim() == "BackgroundNormal" {
                    color = parse_rgb(value);
                }
            }
        }
    }
    color
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_window_color_without_confusing_other_groups() {
        let config = "[Colors:View]\nBackgroundNormal=1,2,3\n\
            [Colors:Window]\n BackgroundNormal = 17, 83, 149 \n\
            [Colors:Button]\nBackgroundNormal=4,5,6\n";
        assert_eq!(parse_window_background(config), Some(0x115395));
        assert_eq!(
            parse_window_background("[Colors:Window]\nBackgroundNormal=255,255,255\n"),
            Some(0xffffff)
        );
    }

    #[test]
    fn missing_or_invalid_colors_use_white_fallback() {
        for value in ["", "-1,0,0", "256,0,0", "1,2", "1,2,3,4", "red", "1,no,3"] {
            let config = format!("[Colors:Window]\nBackgroundNormal={value}\n");
            assert_eq!(
                parse_window_background(&config).unwrap_or(FALLBACK_BACKGROUND),
                0xffffff
            );
        }
        assert_eq!(
            parse_window_background("[Colors:View]\nBackgroundNormal=1,2,3\n"),
            None
        );
    }
}
