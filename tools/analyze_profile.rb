#!/usr/bin/env ruby
# frozen_string_literal: true
#
# analyze_profile.rb - Parse and analyze DEBUG_PROFILING log output from the
# Cxy compiler to identify where unaccounted time is being spent.
#
# Usage:
#   ruby analyze_profile.rb <log_file> [options]
#
# Options:
#   --top N          Show top N gaps (default: 20)
#   --min-gap NS     Only show gaps >= NS nanoseconds (default: 1_000_000 = 1ms)
#   --group          Group gaps by transition type and sum them
#   --timeline       Print full timeline with gaps highlighted

require 'optparse'

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def fmt_ns(ns)
  case ns
  when 0                    then "0ns"
  when 1...1_000            then "#{ns}ns"
  when 1_000...1_000_000    then "%.2fus" % (ns / 1_000.0)
  when 1_000_000...1_000_000_000 then "%.2fms" % (ns / 1_000_000.0)
  else                           "%.3fs"  % (ns / 1_000_000_000.0)
  end
end

def pct(part, total)
  return "  -  " if total.zero?
  "%5.1f%%" % (100.0 * part / total)
end

def basename(path)
  return "(none)" if path.nil?
  File.basename(path)
end

# ---------------------------------------------------------------------------
# Event parsing
# ---------------------------------------------------------------------------

Event = Struct.new(:ts, :type, :file, :stage, :duration, :raw)

LINE_RE = /^\[(\d+)\.(\d+)\] PROFILING: (.+)$/

def parse_event(line)
  m = LINE_RE.match(line.chomp) or return nil
  ts  = m[1].to_i * 1_000_000_000 + m[2].to_i
  msg = m[3]

  type, file, stage, duration =
    case msg
    when /^Context initialized$/                          then [:init]
    when /^Context deinitialized$/                        then [:deinit]
    when /^Enabled$/                                      then [:enabled]
    when /^Created file data for '(.+)'$/                 then [:created, $1]
    when /^START FILE '(.+)'$/                            then [:start_file, $1]
    when /^END FILE '(.+)' \(total=(\d+)ns\)$/           then [:end_file,   $1, nil, $2.to_i]
    when /^PARSE START '(.+)'$/                           then [:parse_start, $1]
    when /^PARSE PAUSE '(.+)' \(elapsed=(\d+)ns.*\)$/    then [:parse_pause, $1, nil, $2.to_i]
    when /^PARSE RESUME '(.+)'$/                          then [:parse_resume, $1]
    when /^PARSE STOP '(.+)' \(total=(\d+)ns\)$/         then [:parse_stop,  $1, nil, $2.to_i]
    when /^STAGE '(.+)' in '(.+)' \((\d+)ns\)$/          then [:stage, $2, $1, $3.to_i]
    when /^C IMPORT \((\d+)ns.*\)$/                       then [:c_import, nil, nil, $1.to_i]
    else                                                       [:unknown]
    end

  Event.new(ts, type, file, stage, duration, msg)
end

# ---------------------------------------------------------------------------
# Gap classification
#
# A "gap" is time between two consecutive log lines that isn't explained by
# any profiled work. We label each gap by the transition it represents.
# ---------------------------------------------------------------------------

def transition_label(a, b)
  "#{a.type} -> #{b.type}"
end

def transition_detail(a, b)
  case [a.type, b.type]
  when [:parse_stop,  :stage]       then "after parse, before first stage  [#{basename(a.file)}]"
  when [:stage,       :stage]       then "between stages #{a.stage} -> #{b.stage}  [#{basename(b.file)}]"
  when [:stage,       :end_file]    then "after last stage, before end_file [#{basename(a.file)}]"
  when [:end_file,    :parse_resume] then "end_file + restore parent parse   [#{basename(a.file)}]"
  when [:parse_resume,:parse_start] then "resumed parent, started child     [#{basename(b.file)}]"
  when [:parse_pause, :created]     then "paused, creating child file data  [#{basename(b.file)}]"
  when [:parse_pause, :start_file]  then "paused, starting child file       [#{basename(b.file)}]"
  when [:c_import,    :parse_resume] then "C import done, resuming parse     [#{basename(b.file)}]"
  when [:c_import,    :stage]       then "C import done, next stage         [#{basename(b.file)}]"
  else
    "#{a.type}[#{basename(a.file)}] -> #{b.type}[#{basename(b.file)}]"
  end
end

# ---------------------------------------------------------------------------
# Main analysis
# ---------------------------------------------------------------------------

options = {
  top:      20,
  min_gap:  1_000_000,   # 1ms
  group:    false,
  timeline: false,
}

OptionParser.new do |o|
  o.banner = "Usage: #{$0} <log_file> [options]"
  o.on("--top N",      Integer, "Show top N gaps (default 20)")     { |v| options[:top]      = v }
  o.on("--min-gap NS", Integer, "Min gap in ns to report (default 1ms)") { |v| options[:min_gap]  = v }
  o.on("--group",               "Group gaps by transition type")    {      options[:group]    = true }
  o.on("--timeline",            "Print full annotated timeline")    {      options[:timeline] = true }
end.parse!

log_file = ARGV[0]
abort "Usage: #{$0} <log_file> [options]" unless log_file
abort "File not found: #{log_file}"       unless File.exist?(log_file)

events = File.foreach(log_file).map { |line| parse_event(line) }.compact
abort "No PROFILING events found in #{log_file}" if events.empty?

wall_ns   = events.last.ts - events.first.ts
total_gap = 0
gaps      = []

events.each_cons(2) do |a, b|
  gap = b.ts - a.ts
  next if gap < options[:min_gap]
  gaps << {
    gap:    gap,
    from:   a,
    to:     b,
    label:  transition_label(a, b),
    detail: transition_detail(a, b),
  }
  total_gap += gap
end

# ---------------------------------------------------------------------------
# Output: grouped summary
# ---------------------------------------------------------------------------

if options[:group]
  puts
  puts "  Gap Summary by Transition Type (>= #{fmt_ns(options[:min_gap])})"
  puts "-" * 72

  grouped = gaps.group_by { |g| g[:label] }
  rows = grouped.map do |label, gs|
    total = gs.sum { |g| g[:gap] }
    { label: label, count: gs.size, total: total, max: gs.map { |g| g[:gap] }.max }
  end.sort_by { |r| -r[:total] }

  name_w  = [rows.map { |r| r[:label].length }.max, 10].max
  total_w = 10
  puts "  %-*s | %*s | %5s | %*s | %s" % [
    name_w, "Transition", total_w, "Total Time", "%", total_w, "Max Gap", "Count"
  ]
  puts "-" * 72
  rows.each do |r|
    puts "  %-*s | %*s | %s | %*s | %d" % [
      name_w, r[:label],
      total_w, fmt_ns(r[:total]),
      pct(r[:total], wall_ns),
      total_w, fmt_ns(r[:max]),
      r[:count]
    ]
  end
  puts "-" * 72
  puts "  %-*s | %*s | %s" % [name_w, "TOTAL GAPS", total_w, fmt_ns(total_gap), pct(total_gap, wall_ns)]
  puts "  %-*s | %*s | %s" % [name_w, "Wall Time",  total_w, fmt_ns(wall_ns),   "100.0%"]
  puts "-" * 72
  puts
end

# ---------------------------------------------------------------------------
# Output: top N individual gaps
# ---------------------------------------------------------------------------

top_gaps = gaps.sort_by { |g| -g[:gap] }.first(options[:top])

puts
puts "  Top #{options[:top]} Individual Gaps >= #{fmt_ns(options[:min_gap])}"
puts "-" * 80
top_gaps.each_with_index do |g, i|
  puts "  #%02d  %s  (%s of wall)" % [i + 1, fmt_ns(g[:gap]).rjust(10), pct(g[:gap], wall_ns)]
  puts "       Context : #{g[:detail]}"
  puts "       From    : #{g[:from].raw}"
  puts "       To      : #{g[:to].raw}"
  puts
end

# ---------------------------------------------------------------------------
# Output: full annotated timeline
# ---------------------------------------------------------------------------

if options[:timeline]
  puts
  puts "  Full Timeline"
  puts "-" * 80
  events.each_cons(2) do |a, b|
    gap = b.ts - a.ts
    marker = gap >= options[:min_gap] ? "  <<< GAP #{fmt_ns(gap)} (#{pct(gap, wall_ns)})" : ""
    puts "  [+#{fmt_ns(a.ts - events.first.ts).rjust(10)}]  #{a.raw}#{marker}"
  end
  last = events.last
  puts "  [+#{fmt_ns(last.ts - events.first.ts).rjust(10)}]  #{last.raw}"
  puts
end

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

puts "  Summary"
puts "-" * 50
puts "  Events parsed : #{events.size}"
puts "  Wall time     : #{fmt_ns(wall_ns)}"
puts "  Gaps >= #{fmt_ns(options[:min_gap]).ljust(6)}: #{fmt_ns(total_gap)} (#{pct(total_gap, wall_ns)})"
puts "  Gaps found    : #{gaps.size}"
puts "-" * 50
puts
