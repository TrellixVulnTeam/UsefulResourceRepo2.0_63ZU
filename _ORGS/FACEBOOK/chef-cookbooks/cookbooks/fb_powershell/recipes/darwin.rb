#
# Cookbook Name:: fb_powershell
# Recipe:: darwin
#
# Copyright (c) 2020-present, Facebook, Inc.
# All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# Darwin == OSX

# Install Package
# This install Powershell core via homebrew. They just call it "powershell"

# Upgrade to latest package if no specific version given
homebrew_cask 'install powershell' do
  cask_name 'powershell'
  only_if { node['fb_powershell']['pwsh']['manage'] }
  action :install
end

# Setup PowerShell Config
fb_powershell_apply_config 'Managing the PowerShell Core config'

# Manage PowerShell Core profiles
fb_powershell_apply_profiles 'Managing the PowerShell profiles' do
  powershell_core true
end
