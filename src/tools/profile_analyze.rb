#!/usr/bin/env ruby
# frozen_string_literal: true

# Analyzes JSON output from: cxy dev --profile=JSON <source>
# Usage:
#   cxy dev --profile=JSON src/main.cxy
#   ruby tools/profile_analyze.rb profiling.json
#
# Options:
#   --top N              Show top N entries (default: 20)
#   --stage STAGE        Filter to specific stage
#   --sort KEY           Sort by: total, avg, max, count (default: total)
#   --mode MODE          Output mode: table, heatmap, timeline, locks (default: table)
#   --output FILE        Save to file instead of stdout

require "json"
require "optparse"

# ── CLI options ────────────────────────────────────────────────────────────────

options = {
  top:    20,
  stage:  nil,
  sort:   "total",
  mode:   "table",
  output: nil,
  input:  "profiling.json"
}

OptionParser.new do |o|
  o.banner = "Usage: #{File.basename($0)} [profiling.json] [options]"
  o.on("--top N",       Integer, "Show top N entries (default: #{options[:top]})") { |v| options[:top] = v }
  o.on("--stage STAGE", String,  "Filter to specific stage") { |v| options[:stage] = v }
  o.on("--sort KEY",    String,  "Sort by: total, avg, max, count (default: #{options[:sort]})") { |v| options[:sort] = v }
  o.on("--mode MODE",   String,  "Mode: table, heatmap, timeline, locks (default: #{options[:mode]})") { |v| options[:mode] = v }
  o.on("--output FILE", String,  "Save output to file") { |v| options[:output] = v }
  o.on("-h", "--help") { puts o; exit }
end.parse!

options[:input] = ARGV.first if ARGV.first

# ── Color constants ────────────────────────────────────────────────────────────

BOLD  = "\e[1m"
RESET = "\e[0m"
RED   = "\e[31m"
YEL   = "\e[33m"
GRN   = "\e[32m"
BLU   = "\e[34m"
MAG   = "\e[35m"
CYN   = "\e[36m"
DIM   = "\e[2m"

# ── Load JSON ──────────────────────────────────────────────────────────────────

unless File.exist?(options[:input])
  warn "Error: #{options[:input]} not found"
  warn "Run: cxy dev --profile=JSON <file.cxy>"
  exit 1
end

data = JSON.parse(File.read(options[:input]))

unless data["profilingEnabled"]
  warn "Error: Profiling was not enabled in this data"
  exit 1
end

entries = data["entries"] || []

if entries.empty?
  warn "Error: No profiling entries found"
  exit 1
end

# ── Helper functions ───────────────────────────────────────────────────────────

def ns_to_ms(ns)
  (ns / 1_000_000.0).round(2)
end

def ns_to_s(ns)
  ms = ns / 1_000_000.0
  if ms >= 1000
    "#{(ms / 1000).round(2)}s"
  elsif ms >= 1
    "#{ms.round(2)}ms"
  elsif ns >= 1000
    "#{(ns / 1000).round(2)}μs"
  else
    "#{ns}ns"
  end
end

def ms_color(ms)
  if    ms >= 100 then RED
  elsif ms >= 10  then YEL
  else                 GRN
  end
end

def heat_color(ratio)
  # ratio from 0.0 to 1.0
  if    ratio >= 0.8 then RED
  elsif ratio >= 0.5 then YEL
  elsif ratio >= 0.2 then GRN
  else                    DIM
  end
end

def bar(value, max_value, width: 40)
  return "░" * width if max_value == 0
  filled = (value.to_f / max_value * width).round
  filled = [[filled, 0].max, width].min
  "█" * filled + "░" * (width - filled)
end

def heat_bar(value, max_value, width: 40)
  return "" if max_value == 0
  ratio = value.to_f / max_value
  filled = (ratio * width).round
  filled = [[filled, 0].max, width].min

  color = heat_color(ratio)
  "#{color}#{"█" * filled}#{RESET}#{DIM}#{"░" * (width - filled)}#{RESET}"
end

def abbreviate(text, max_len)
  return text if text.length <= max_len
  "...#{text[-max_len+3..]}"
end

# ── Separate entries by type ───────────────────────────────────────────────────

files = entries.select { |e| e["depth"] == 0 && !e["isLockProfile"] }
stages = entries.select { |e| e["depth"] > 0 && !e["isLockProfile"] }
locks = entries.select { |e| e["isLockProfile"] }

# ── Filter by stage if requested ───────────────────────────────────────────────

if options[:stage]
  stages = stages.select { |e| e["name"] == options[:stage] }
  if stages.empty?
    available = entries.map { |e| e["name"] }.uniq
    warn "Stage '#{options[:stage]}' not found"
    warn "Available: #{available.join(", ")}"
    exit 1
  end
end

# ── Output redirection ─────────────────────────────────────────────────────────

output = options[:output] ? File.open(options[:output], "w") : $stdout

# ── Mode: Table ────────────────────────────────────────────────────────────────

def print_table(files, stages, locks, options, output)
  output.puts
  output.puts "#{BOLD}Cxy Profiling Report#{RESET}"
  output.puts "#{DIM}━" * 80 + "#{RESET}"
  output.puts

  # ── Files section ────────────────────────────────────────────────────
  if files.any?
    output.puts "#{BOLD}Compilation Units#{RESET}"
    output.puts

    sort_key = options[:sort].to_sym
    sorted = case sort_key
             when :total then files.sort_by { |f| -f["totalNs"] }
             when :avg   then files.sort_by { |f| -f["avgNs"] }
             when :max   then files.sort_by { |f| -f["maxNs"] }
             when :count then files.sort_by { |f| -f["count"] }
             else files.sort_by { |f| -f["totalNs"] }
             end

    top = sorted.first(options[:top])
    max_total = top.map { |f| f["totalNs"] }.max || 1

    output.puts "  #{"File".ljust(45)}  #{"Total".rjust(10)}  #{"Avg".rjust(10)}  #{"Max".rjust(10)}  #{"Calls".rjust(6)}  Heat"
    output.puts "  #{DIM}#{"─" * 45}  #{"─" * 10}  #{"─" * 10}  #{"─" * 10}  #{"─" * 6}  #{"─" * 20}#{RESET}"

    top.each do |f|
      name = abbreviate(File.basename(f["name"]), 45).ljust(45)
      total = ns_to_ms(f["totalNs"])
      avg = ns_to_ms(f["avgNs"])
      max = ns_to_ms(f["maxNs"])
      count = f["count"]

      tc = ms_color(total)
      heat = heat_bar(f["totalNs"], max_total, width: 20)

      output.puts "  #{name}  #{tc}#{total.to_s.rjust(8)}ms#{RESET}  #{avg.to_s.rjust(8)}ms  #{max.to_s.rjust(8)}ms  #{count.to_s.rjust(6)}  #{heat}"
    end
    output.puts
  end

  # ── Stages section ───────────────────────────────────────────────────
  if stages.any?
    output.puts "#{BOLD}Compilation Stages#{RESET}"
    output.puts

    # Group stages by name and aggregate
    stage_map = {}
    stages.each do |s|
      name = s["name"]
      stage_map[name] ||= { "totalNs" => 0, "count" => 0, "maxNs" => 0, "minNs" => Float::INFINITY }
      stage_map[name]["totalNs"] += s["totalNs"]
      stage_map[name]["count"] += s["count"]
      stage_map[name]["maxNs"] = [stage_map[name]["maxNs"], s["maxNs"]].max
      stage_map[name]["minNs"] = [stage_map[name]["minNs"], s["minNs"]].min
    end

    # Convert to array and add avgNs
    stage_list = stage_map.map do |name, data|
      data.merge("name" => name, "avgNs" => data["count"] > 0 ? data["totalNs"] / data["count"] : 0)
    end

    sort_key = options[:sort].to_sym
    sorted = case sort_key
             when :total then stage_list.sort_by { |s| -s["totalNs"] }
             when :avg   then stage_list.sort_by { |s| -s["avgNs"] }
             when :max   then stage_list.sort_by { |s| -s["maxNs"] }
             when :count then stage_list.sort_by { |s| -s["count"] }
             else stage_list.sort_by { |s| -s["totalNs"] }
             end

    max_total = sorted.map { |s| s["totalNs"] }.max || 1
    total_time = stage_list.sum { |s| s["totalNs"] }

    output.puts "  #{"Stage".ljust(20)}  #{"Total".rjust(10)}  #{"Avg".rjust(10)}  #{"Max".rjust(10)}  #{"Calls".rjust(6)}  #{"Pct".rjust(6)}  Heat"
    output.puts "  #{DIM}#{"─" * 20}  #{"─" * 10}  #{"─" * 10}  #{"─" * 10}  #{"─" * 6}  #{"─" * 6}  #{"─" * 30}#{RESET}"

    sorted.each do |s|
      name = s["name"].ljust(20)
      total = ns_to_ms(s["totalNs"])
      avg = ns_to_ms(s["avgNs"])
      max = ns_to_ms(s["maxNs"])
      count = s["count"]
      pct = total_time > 0 ? (s["totalNs"].to_f / total_time * 100).round(1) : 0

      tc = ms_color(total)
      heat = heat_bar(s["totalNs"], max_total, width: 30)

      output.puts "  #{name}  #{tc}#{total.to_s.rjust(8)}ms#{RESET}  #{avg.to_s.rjust(8)}ms  #{max.to_s.rjust(8)}ms  #{count.to_s.rjust(6)}  #{pct.to_s.rjust(5)}%  #{heat}"
    end
    output.puts
  end

  # ── Lock contention section ──────────────────────────────────────────
  if locks.any?
    output.puts "#{BOLD}Lock Contention Analysis#{RESET}"
    output.puts

    max_wait = locks.map { |l| l["waitTimeNs"] }.max || 1
    max_hold = locks.map { |l| l["holdTimeNs"] }.max || 1

    output.puts "  #{"Lock".ljust(30)}  #{"Wait".rjust(10)}  #{"Hold".rjust(10)}  #{"Acq.".rjust(6)}  Contention"
    output.puts "  #{DIM}#{"─" * 30}  #{"─" * 10}  #{"─" * 10}  #{"─" * 6}  #{"─" * 30}#{RESET}"

    locks.sort_by { |l| -l["waitTimeNs"] }.each do |l|
      name = abbreviate(l["name"], 30).ljust(30)
      wait = ns_to_ms(l["waitTimeNs"])
      hold = ns_to_ms(l["holdTimeNs"])
      count = l["count"]

      wait_bar = heat_bar(l["waitTimeNs"], max_wait, width: 30)

      output.puts "  #{name}  #{RED}#{wait.to_s.rjust(8)}ms#{RESET}  #{YEL}#{hold.to_s.rjust(8)}ms#{RESET}  #{count.to_s.rjust(6)}  #{wait_bar}"
    end
    output.puts
  end
end

# ── Mode: Heatmap ──────────────────────────────────────────────────────────────

def print_heatmap(files, stages, options, output)
  output.puts
  output.puts "#{BOLD}Compilation Heatmap#{RESET}"
  output.puts "#{DIM}━" * 80 + "#{RESET}"
  output.puts

  # Group stages by name
  stage_map = {}
  stages.each do |s|
    name = s["name"]
    stage_map[name] ||= 0
    stage_map[name] += s["totalNs"]
  end

  stage_names = stage_map.keys.sort
  file_names = files.map { |f| File.basename(f["name"]) }.sort

  return if stage_names.empty? || file_names.empty?

  # Build matrix: files x stages
  max_val = 0
  matrix = {}

  files.each do |file|
    fname = File.basename(file["name"])
    matrix[fname] = {}

    stages.each do |stage|
      next unless stage["depth"] > 0
      stage_name = stage["name"]

      # Aggregate by finding all stages with matching depth level
      # (simplified: use totalNs / count as approximation)
      val = stage["totalNs"] / [stage["count"], 1].max
      matrix[fname][stage_name] ||= 0
      matrix[fname][stage_name] += val
      max_val = [max_val, matrix[fname][stage_name]].max
    end
  end

  # Print header
  output.print "  #{"File".ljust(30)}"
  stage_names.each do |sname|
    output.print "  #{sname[0..7].rjust(8)}"
  end
  output.puts

  output.print "  #{DIM}#{"─" * 30}"
  stage_names.each { output.print "  #{"─" * 8}" }
  output.puts "#{RESET}"

  # Print rows
  file_names.first([options[:top], file_names.size].min).each do |fname|
    output.print "  #{abbreviate(fname, 30).ljust(30)}"

    stage_names.each do |sname|
      val = matrix[fname][sname] || 0
      ratio = max_val > 0 ? val.to_f / max_val : 0

      color = heat_color(ratio)
      ms = ns_to_ms(val)

      output.print "  #{color}#{ms.to_s.rjust(8)}#{RESET}"
    end
    output.puts
  end
  output.puts

  output.puts "#{DIM}Legend: #{RED}█#{RESET} hot  #{YEL}█#{RESET} warm  #{GRN}█#{RESET} cool#{RESET}"
  output.puts
end

# ── Mode: Timeline ─────────────────────────────────────────────────────────────

def print_timeline(files, stages, options, output)
  output.puts
  output.puts "#{BOLD}Compilation Timeline#{RESET}"
  output.puts "#{DIM}━" * 80 + "#{RESET}"
  output.puts

  # Create timeline based on depth
  all = (files + stages).sort_by { |e| [e["depth"], e["name"]] }

  max_total = all.map { |e| e["totalNs"] }.max || 1

  all.first(options[:top]).each do |e|
    indent = "  " * e["depth"]
    name = abbreviate(e["name"], 40 - indent.length)
    total = ns_to_ms(e["totalNs"])

    bar_width = 40
    filled = (e["totalNs"].to_f / max_total * bar_width).round
    filled = [[filled, 0].max, bar_width].min

    color = ms_color(total)
    heat = "#{color}#{"█" * filled}#{RESET}#{DIM}#{"░" * (bar_width - filled)}#{RESET}"

    output.puts "#{indent}#{name.ljust(40 - indent.length)}  #{color}#{total.to_s.rjust(8)}ms#{RESET}  #{heat}"
  end
  output.puts
end

# ── Mode: Locks ────────────────────────────────────────────────────────────────

def print_locks(locks, options, output)
  output.puts
  output.puts "#{BOLD}Lock Contention Deep Dive#{RESET}"
  output.puts "#{DIM}━" * 80 + "#{RESET}"
  output.puts

  if locks.empty?
    output.puts "#{DIM}No lock profiling data found.#{RESET}"
    output.puts "#{DIM}This is expected if CXY_PARALLEL_COMPILE is not enabled.#{RESET}"
    output.puts
    return
  end

  total_wait = locks.sum { |l| l["waitTimeNs"] }
  total_hold = locks.sum { |l| l["holdTimeNs"] }
  total_acquisitions = locks.sum { |l| l["count"] }

  output.puts "#{BOLD}Summary#{RESET}"
  output.puts "  Total wait time:    #{RED}#{ns_to_s(total_wait)}#{RESET}"
  output.puts "  Total hold time:    #{YEL}#{ns_to_s(total_hold)}#{RESET}"
  output.puts "  Total acquisitions: #{total_acquisitions}"
  output.puts

  output.puts "#{BOLD}Lock Details#{RESET}"
  output.puts

  sorted = locks.sort_by { |l| -l["waitTimeNs"] }
  max_wait = sorted.first&.fetch("waitTimeNs", 1) || 1

  sorted.first(options[:top]).each do |l|
    name = l["name"]
    wait = l["waitTimeNs"]
    hold = l["holdTimeNs"]
    count = l["count"]
    avg_wait = count > 0 ? wait / count : 0
    avg_hold = count > 0 ? hold / count : 0

    output.puts "  #{BOLD}#{name}#{RESET}"
    output.puts "    Acquisitions:  #{count}"
    output.puts "    Wait time:     #{RED}#{ns_to_s(wait)}#{RESET} (avg: #{ns_to_s(avg_wait)})"
    output.puts "    Hold time:     #{YEL}#{ns_to_s(hold)}#{RESET} (avg: #{ns_to_s(avg_hold)})"

    wait_pct = total_wait > 0 ? (wait.to_f / total_wait * 100).round(1) : 0
    output.puts "    Wait %:        #{wait_pct}%"

    heat = heat_bar(wait, max_wait, width: 40)
    output.puts "    Contention:    #{heat}"
    output.puts
  end
end

# ── Main dispatch ──────────────────────────────────────────────────────────────

case options[:mode].downcase
when "table"
  print_table(files, stages, locks, options, output)
when "heatmap"
  print_heatmap(files, stages, options, output)
when "timeline"
  print_timeline(files, stages, options, output)
when "locks"
  print_locks(locks, options, output)
else
  warn "Unknown mode: #{options[:mode]}"
  warn "Available modes: table, heatmap, timeline, locks"
  exit 1
end

# ── Summary ────────────────────────────────────────────────────────────────────

total_time = files.sum { |f| f["totalNs"] }
total_files = files.size
total_stages = stages.size

output.puts "#{DIM}Total compilation time: #{ns_to_s(total_time)} across #{total_files} file(s)#{RESET}"
output.puts "#{DIM}Tip: Try --mode heatmap or --mode locks for different views#{RESET}"
output.puts

output.close if options[:output]

puts "#{GRN}✓#{RESET} Report saved to #{options[:output]}" if options[:output]
