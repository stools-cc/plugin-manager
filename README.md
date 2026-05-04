# stools Plugin Manager

OBS Studio plugin that manages stools plugins. Automatically downloads, installs and updates plugins with login authentication.

## Features

- Login via stools.cc API token
- Automatic plugin download and installation
- Auto-update check on OBS startup
- Plugin version tracking
- Cross-platform (Windows, macOS, Linux)

## Building

```bash
# Windows (requires Visual Studio + OBS installed)
powershell -ExecutionPolicy Bypass -File build.ps1
```

## License

This project is licensed under the [GNU General Public License v2.0](LICENSE).
