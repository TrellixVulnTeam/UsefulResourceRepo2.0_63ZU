fb_fluentbit Cookbook
========================
This cookbook installs the Fluent Bit daemon and optionally plugins for it.

Attributes
----------
* node['fb_fluentbit']['service_config']
* node['fb_fluentbit']['parsers']
* node['fb_fluentbit']['input']
* node['fb_fluentbit']['filter']
* node['fb_fluentbit']['output']
* node['fb_fluentbit']['external']
* node['fb_fluentbit']['external_config_url']

Usage
-----
Include `fb_fluentbit::default` recipe to install fluentbit. For more
information about how to configure Fluent Bit, please refer to
[the documentation](https://docs.fluentbit.io/manual/)

This cookbook provides 2 options to create fluentbit config:
* Fetching existing config from url
* Configuration template

Fluentbit allows you to have multiple instances of the same plugins, for example
multiple tail plugin instances to get logs from multiple files.

Fluentbit supports built-in and external plugins.

### Fetching remote config
This cookbook can configure fluentbit to fetch its config from a remote
location. To do so, set `node['fb_fluentbit']['external_config_url']` to the
address at which to receive a custom config.

When operating in this mode, this completely bypasses any other node attributes
set on this cookbook.

### Configuring builtin plugins
Fluentbit comes with a number of different builtin plugins that can be
configured via the cookbook API. Each plugin is scoped by a key name (that is
not used in the final config file).

As an example, the following configures an input tailing a log file, filtering
out certain lines based on grep, and ultimately logging to stdout.

```
# Read lines from /var/log/my_log.txt
node.default['fb_fluentd']['input']['tail']['tail_myfile'] = {
  'Path' => '/var/log/my_log.txt',
  'Tag' => 'my_cool_log',
}

# Filter out lines that contain "badstuff"
node.default['fb_fluentd']['filter']['grep']['filter_badstuff'] = {
  'Match' => 'my_cool_log',
  'Regex' => '^.+badstuff.+$',
}

# Finally, output to stdout
node.default['fb_fluentd']['output']['stdout']['print_to_output'] = {
  'Match' => 'my_cool_log',
}
```

This will be rendered as:

```
[INPUT]
    Name tail
    Path /var/log/my_log.txt
    Tag my_cool_log

[FILTER]
    Name grep
    Match my_cool_tag
    Regex ^.+badstuff.+$

[OUTPUT]
    Name stdout
    Match my_cool_log
```

### External plugins
Fluent Bit supports external/custom plugins. This cookbook API allows for
providing a combination of RPM package name and plugin path like so:

```
# Tell fb_fluentbit where and how to configure your plugin.
node.default['fb_fluentbit']['external']['my_plugin'] = {
  'package' => 'my-rpm-package',
  'path' => '/usr/local/lib/my_plugin/my_plugin.so',
}
```

Then configure your plugin just like any other builtin plugin:

```
node.default['fb_fluentbit']['output']['my_plugin']['foo'] = {
  'Match' => '*',
  'Key_1' => 'Value_1',
}
```

### Custom Parsers
Parsers can be configured by setting their name and configuration in
`node['fb_fluentbit']['parser']` like so:

```
node.default['fb_fluentbit']['parser']['some_parser'] = {
  'format' => 'json',
  'Time_Key' => 'time',
  'Time_Format' => '%Y-%m-%dT%H:%M:%S %z',
}
```

This will be rendered in parsers.conf as:

```
[PARSER]
    Name some_parser
    Format json
    Time_Key time
    Time_Format %Y-%m-%dT%H:%M:%S %z
```
