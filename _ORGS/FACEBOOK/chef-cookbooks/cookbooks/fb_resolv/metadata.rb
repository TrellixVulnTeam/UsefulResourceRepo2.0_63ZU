# Copyright (c) 2016-present, Facebook, Inc.
name 'fb_resolv'
maintainer 'Facebook'
maintainer_email 'noreply@facebook.com'
license 'Apache-2.0'
description 'Installs/Configures resolv'
source_url 'https://github.com/facebook/chef-cookbooks/'
long_description IO.read(File.join(File.dirname(__FILE__), 'README.md'))
version '0.0.1'
supports 'centos'
supports 'debian'
supports 'fedora'
supports 'ubuntu'
depends 'fb_helpers'
