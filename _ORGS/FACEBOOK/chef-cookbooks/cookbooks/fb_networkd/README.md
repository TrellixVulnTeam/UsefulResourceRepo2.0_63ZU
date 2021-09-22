fb_networkd Cookbook
====================
Configure and manage networking using systemd-networkd.

Requirements
------------

Attributes
----------
* node['fb_networkd']['primary_interface']
* node['fb_networkd']['networks'][$NETWORK]['priority']
* node['fb_networkd']['networks'][$NETWORK]['config']
* node['fb_networkd']['links'][$LINK]['priority']
* node['fb_networkd']['links'][$LINK]['config']
* node['fb_networkd']['devices'][$DEVICE]['priority']
* node['fb_networkd']['devices'][$DEVICE]['config']

Usage
-----
Include `fb_networkd` to configure networkd on your system. This cookbook
leverages `fb_systemd` to install and enable the `systemd-networkd` service.

By default, `fb_networkd` will define a stub config for the primary interface,
which is denoted by `node['fb_networkd']['primary_interface']` and defaults to
`eth0`. If this is changed, you may want to also remove the default config:

```ruby
node.default['fb_networkd']['primary_interface'] = 'eth2'
node.default['fb_networkd']['networks'].delete('eth0')
```

Add networks, links and virtual network devices configurations to the
respective attributes, e.g.:

```ruby
node.default['fb_networkd']['networks']['eth0'] = {
  'priority' => 1,
  'config' => {
    'Network' => {
      'Address' => [
        '2001:db00::1/64',
        '192.168.1.1/24',
        '2401:db00::1/64',
      ],
    },
    'Address' => [
      {
        'Address' => '2001:db00::1/64',
        'PreferredLifetime' => 'infinity',
      },
      {
        'Address' => '2401:db00::1/64',
        'PreferredLifetime' => '0',
      },
    ],
  }
}
```

According to the systemd.netdev man page, virtual network devices are created as
soon as systemd-networkd is started. And if an existing network device with a
specified name already exists, systemd-networkd will use it as-is rather than
create its own. Thus, we advise against creating networks and devices with the
same name.

Refer to the upstream documentation for more details on how to configure
[networks](https://www.freedesktop.org/software/systemd/man/systemd.network.html),
[links](https://www.freedesktop.org/software/systemd/man/systemd.link.html) and
[virtual network devices](https://www.freedesktop.org/software/systemd/man/systemd.netdev.html).
