# SPDX-License-Identifier: BSL-1.0
# SPDX-FileCopyrightText: Copyright 2024-2025 Kasumi Hanazuki <kasumi@rollingapple.net>

require 'bundler/gem_tasks'
require 'rspec/core/rake_task'
require 'rake/extensiontask'

Rake::ExtensionTask.new('test') do |t|
  t.ext_dir = 'spec/ext/test'
  t.lib_dir = 'spec/'
end

file 'compile_commands.json' => FileList['tmp/**/*.o.json'] do |t|
  json = t.prerequisites.map { File.read(it).chomp }.join.chomp(?,)
  json = "[#{json}]"
  File.write(t.name, json)
end

task :compile => 'compile_commands.json'

RSpec::Core::RakeTask.new(:spec)
task :spec => :compile

task :default => :spec
