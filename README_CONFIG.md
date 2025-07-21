# SimFS Configuration Guide

SimFS now supports per-directory configuration through `.simfs_config.toml` files.

## How It Works

1. Create a file named `.simfs_config.toml` in any directory within your SimFS mount
2. SimFS will use the configuration from that file when generating content for files in that directory
3. Configuration is inherited from parent directories - the most specific configuration wins

## Configuration File Format

The configuration file uses TOML format. Currently supported options:

```toml
# Model name to use for this directory
model = "gpt-3.5-turbo"
```

## Example Usage

1. Mount SimFS:
   ```bash
   ./simfs /tmp/simfs_mount --llm-endpoint http://localhost:5001/v1/chat/completions
   ```

2. Create a configuration file:
   ```bash
   echo 'model = "gpt-4"' > /tmp/simfs_mount/docs/.simfs_config.toml
   ```

3. Files generated in `/tmp/simfs_mount/docs/` will now use the "gpt-4" model

## Configuration Inheritance

- Root directory config applies to all files unless overridden
- Subdirectory configs override parent directory configs
- The most specific (deepest) configuration is used

## Clearing Config Cache

The configuration is cached for performance. The cache is automatically cleared when any `.simfs_config.toml` file is modified.