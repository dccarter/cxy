#!/usr/bin/env ruby
# frozen_string_literal: true
#
# profile_analyze.rb — Analyze profiling.json output from the Cxy compiler.
#
# Usage:
#   cxy dev --profile=JSON src/main.cxy
#   ruby tools/profile_analyze.rb [profiling.json] [options]
#
# Modes:
#   table     Per-file stage breakdown table (default)
#   heatmap   ASCII heatmap: files × stages, colour-coded by % of column max
#   timeline  Horizontal bar chart showing each file's total time
#   summary   Stage totals only

require "json"
require "optparse"

# ── ANSI colours ────────────────────────────────────────────────────────────

module C
  RESET  = "\e[0m"
  BOLD   = "\e[1m"
  DIM    = "\e[2m"

  # heat levels (cool → hot)
  HEAT = [
    "\e[38;5;17m",   # 0-10%   dark blue
    "\e[38;5;27m",   # 10-20%  blue
    "\e[38;5;33m",   # 20-30%  cyan-blue
    "\e[38;5;37m",   # 30-40%  teal
    "\e[38;5;226m",  # 40-55%  yellow
    "\e[38;5;208m",  # 55-70%  orange
    "\e[38;5;196m",  # 70-85%  red
    "\e[38;5;199m",  # 85-100% magenta-red
  ].freeze

  BAR_COLOURS = [
    "\e[34m",  # blue
    "\e[36m",  # cyan
    "\e[32m",  # green
    "\e[33m",  # yellow
    "\e[35m",  # magenta
    "\e[31m",  # red
  ].freeze

  def self.heat(ratio)
    idx = (ratio * (HEAT.size - 1)).round.clamp(0, HEAT.size - 1)
    HEAT[idx]
  end

  def self.bar(idx)
    BAR_COLOURS[idx % BAR_COLOURS.size]
  end

  def self.enabled?
    @enabled
  end

  def self.enabled=(v)
    @enabled = v
  end

  def self.apply(code, text)
    @enabled ? "#{code}#{text}#{RESET}" : text
  end
end

C.enabled = $stdout.tty?

# ── Formatting helpers ───────────────────────────────────────────────────────

def fmt_ns(ns)
  return "-" if ns.nil? || ns == 0
  case ns
  when 0...1_000            then "#{ns}ns"
  when 1_000...1_000_000    then "%.2fus" % (ns / 1_000.0)
  when 1_000_000...1_000_000_000 then "%.2fms" % (ns / 1_000_000.0)
  else                           "%.3fs"  % (ns / 1_000_000_000.0)
  end
end

def pct(part, total)
  return "     -" if total.nil? || total.zero?
  "%5.1f%%" % (100.0 * part / total)
end

def basename(path)
  File.basename(path.to_s)
end

def divider(width, char = "-")
  char * width
end

# ── Data loading ─────────────────────────────────────────────────────────────

def load_profile(path)
  raw = JSON.parse(File.read(path))

  unless raw["profilingEnabled"]
    abort "Profiling was not enabled when this file was generated."
  end

  files  = raw["files"] || []
  totals = raw["totals"] || {}

  # Collect the ordered set of stages that appear across all files
  stage_names = []
  files.each do |f|
    (f["stages"] || {}).each_key do |s|
      stage_names << s unless stage_names.include?(s)
    end
  end

  {
    files:           files,
    totals:          totals,
    stage_names:     stage_names,
    total_files:     raw["totalFiles"] || files.size,
    c_import_ns:     raw["cImportTimeNs"] || 0,
  }
end

# ── Mode: table ──────────────────────────────────────────────────────────────

def mode_table(data, opts)
  files        = data[:files]
  stage_names  = data[:stage_names]
  totals       = data[:totals]
  wall_ns      = data[:totals]["totalTimeNs"] || 0
  c_import_ns  = data[:c_import_ns]

  # Column widths
  name_w   = [4, files.map { |f| basename(f["fileName"]).length }.max || 4].max
  parse_w  = [5, files.map { |f| fmt_ns(f["parseTimeNs"]).length }.max,
                 fmt_ns(totals["parseTimeNs"]).length].max
  total_w  = [5, files.map { |f| fmt_ns(f["totalTimeNs"]).length }.max,
                 fmt_ns(totals["totalTimeNs"]).length].max

  stage_w = {}
  stage_names.each do |s|
    stage_w[s] = [s.length,
                  files.map { |f| fmt_ns((f["stages"] || {})[s]).length }.max || 1,
                  fmt_ns((totals["stageTimesNs"] || {})[s]).length].max
  end

  # Total row width
  row_w = name_w + 3 + parse_w + 3 +
          stage_names.sum { |s| stage_w[s] + 3 } +
          total_w

  sep = divider(row_w + 2)

  out = opts[:out]
  out.puts
  out.puts C.apply(C::BOLD, "  Per-File Stage Breakdown")
  out.puts sep

  # Header
  header = "  " +
    "%-*s" % [name_w, "File"] + " | " +
    "%*s" % [parse_w, "Parse"] + " | " +
    stage_names.map { |s| "%*s" % [stage_w[s], s] }.join(" | ") + " | " +
    "%*s" % [total_w, "Total"]
  out.puts C.apply(C::BOLD, header)
  out.puts sep

  files.each do |f|
    stages = f["stages"] || {}
    row = "  " +
      "%-*s" % [name_w, basename(f["fileName"])] + " | " +
      "%*s" % [parse_w, fmt_ns(f["parseTimeNs"])] + " | " +
      stage_names.map { |s|
        v = stages[s]
        "%*s" % [stage_w[s], v && v > 0 ? fmt_ns(v) : "-"]
      }.join(" | ") + " | " +
      "%*s" % [total_w, fmt_ns(f["totalTimeNs"])]
    out.puts row
  end

  out.puts sep

  # Totals row
  st = totals["stageTimesNs"] || {}
  total_row = "  " +
    "%-*s" % [name_w, C.apply(C::BOLD, "TOTAL")] + " | " +
    "%*s" % [parse_w, fmt_ns(totals["parseTimeNs"])] + " | " +
    stage_names.map { |s| "%*s" % [stage_w[s], fmt_ns(st[s])] }.join(" | ") + " | " +
    "%*s" % [total_w, fmt_ns(totals["totalTimeNs"])]
  out.puts total_row
  out.puts sep

  # ── Stage totals table ──────────────────────────────────────────────────────
  all_stages = [["Parse", totals["parseTimeNs"]]] +
               stage_names.map { |s| [s, st[s] || 0] }
  all_stages << ["C Import", c_import_ns] if c_import_ns > 0

  accounted_ns = (totals["totalTimeNs"] || 0) + c_import_ns

  s_name_w  = [11, all_stages.map { |s, _| s.length }.max].max   # "Unaccounted"
  s_total_w = [10, all_stages.map { |_, ns| fmt_ns(ns).length }.max,
                   fmt_ns(accounted_ns).length, fmt_ns(wall_ns).length].max
  s_pct_w   = 6
  s_files_w = 5
  s_row_w   = s_name_w + 3 + s_total_w + 3 + s_pct_w + 3 + s_files_w

  out.puts
  out.puts C.apply(C::BOLD, "  Stage Totals")
  out.puts divider(s_row_w + 2)
  out.puts "  %-*s | %*s | %*s | %*s" % [
    s_name_w, C.apply(C::BOLD, "Stage"),
    s_total_w, "Total Time",
    s_pct_w,  "%",
    s_files_w, "Files"
  ]
  out.puts divider(s_row_w + 2)

  all_stages.each do |name, ns|
    file_count = if name == "Parse"
      data[:files].size
    elsif name == "C Import"
      "-"
    else
      data[:files].count { |f| ((f["stages"] || {})[name] || 0) > 0 }
    end
    out.puts "  %-*s | %*s | %*s | %*s" % [
      s_name_w, name,
      s_total_w, fmt_ns(ns),
      s_pct_w,  pct(ns, wall_ns),
      s_files_w, file_count.to_s
    ]
  end

  out.puts divider(s_row_w + 2)
  out.puts "  %-*s | %*s | %*s" % [s_name_w, "Accounted",   s_total_w, fmt_ns(accounted_ns), s_pct_w, pct(accounted_ns, wall_ns)]
  out.puts "  %-*s | %*s | %*s" % [s_name_w, "Unaccounted", s_total_w, fmt_ns([wall_ns - accounted_ns, 0].max), s_pct_w, pct([wall_ns - accounted_ns, 0].max, wall_ns)]
  out.puts divider(s_row_w + 2)
  out.puts C.apply(C::BOLD, "  %-*s | %*s | %*s" % [s_name_w, "Wall Time", s_total_w, fmt_ns(wall_ns), s_pct_w, "100.0%"])
  out.puts divider(s_row_w + 2)
  out.puts
end

# ── Mode: heatmap ────────────────────────────────────────────────────────────

def mode_heatmap(data, opts)
  files       = data[:files]
  stage_names = data[:stage_names]
  out         = opts[:out]

  # All columns = Parse + stages
  cols = ["Parse"] + stage_names

  # Column max values (for normalisation)
  col_max = {}
  cols.each do |col|
    col_max[col] = files.map { |f|
      col == "Parse" ? f["parseTimeNs"] : (f["stages"] || {})[col] || 0
    }.max || 1
  end

  name_w  = [4, files.map { |f| basename(f["fileName"]).length }.max].max
  cell_w  = [cols.map(&:length).max, 8].max

  row_w = name_w + 3 + cols.size * (cell_w + 3)
  sep   = divider(row_w)

  out.puts
  out.puts C.apply(C::BOLD, "  Heatmap — time per stage (colour = % of column maximum)")
  out.puts sep

  # Header
  out.print "  " + C.apply(C::BOLD, "%-*s" % [name_w, "File"])
  cols.each { |c| out.print " | " + C.apply(C::BOLD, "%*s" % [cell_w, c]) }
  out.puts
  out.puts sep

  files.each do |f|
    out.print "  %-*s" % [name_w, basename(f["fileName"])]
    cols.each do |col|
      ns    = col == "Parse" ? f["parseTimeNs"] : (f["stages"] || {})[col] || 0
      ratio = col_max[col] > 0 ? ns.to_f / col_max[col] : 0
      cell  = "%*s" % [cell_w, fmt_ns(ns)]
      out.print " | " + C.apply(C::heat(ratio), cell)
    end
    out.puts
  end

  out.puts sep

  # Legend
  out.puts
  out.print "  Heat scale: "
  C::HEAT.each_with_index do |code, i|
    label = "%d-%d%%" % [i * (100 / C::HEAT.size), (i + 1) * (100 / C::HEAT.size)]
    out.print C.apply(code, " #{label} ")
  end
  out.puts
  out.puts
end

# ── Mode: timeline ───────────────────────────────────────────────────────────

def mode_timeline(data, opts)
  files   = data[:files]
  out     = opts[:out]
  bar_w   = opts[:bar_width] || 50

  max_ns  = files.map { |f| f["totalTimeNs"] || 0 }.max || 1
  name_w  = [4, files.map { |f| basename(f["fileName"]).length }.max].max
  total_w = files.map { |f| fmt_ns(f["totalTimeNs"]).length }.max

  # Stage colours — fixed order for legend
  stage_names = data[:stage_names]
  all_segs    = ["Parse"] + stage_names
  seg_colour  = all_segs.each_with_index.map { |s, i| [s, C::BAR_COLOURS[i % C::BAR_COLOURS.size]] }.to_h

  out.puts
  out.puts C.apply(C::BOLD, "  Timeline — total time per file (bar width = #{bar_w} chars)")
  out.puts

  files.sort_by { |f| -(f["totalTimeNs"] || 0) }.each do |f|
    total = f["totalTimeNs"] || 0
    segs  = { "Parse" => f["parseTimeNs"] || 0 }
    (f["stages"] || {}).each { |s, ns| segs[s] = ns }

    # Build bar segments proportional to total
    bar = +""
    all_segs.each do |seg|
      ns    = segs[seg] || 0
      chars = (ns.to_f / max_ns * bar_w).round
      next if chars == 0
      colour = seg_colour[seg] || C::RESET
      bar << C.apply(colour, "█" * chars)
    end

    # Pad bar to full width (for alignment)
    bar_len = (total.to_f / max_ns * bar_w).round
    # (padding not needed since we're printing label after)

    out.puts "  %-*s  %s  %*s  %s" % [
      name_w, basename(f["fileName"]),
      bar,
      total_w, fmt_ns(total),
      C.apply(C::DIM, pct(total, data[:totals]["totalTimeNs"]))
    ]
  end

  # Legend
  out.puts
  out.print "  Segments: "
  all_segs.each_with_index do |seg, i|
    out.print C.apply(C::BAR_COLOURS[i % C::BAR_COLOURS.size], "█ #{seg}  ")
  end
  out.puts "\n"
end

# ── Mode: summary ────────────────────────────────────────────────────────────

def mode_summary(data, opts)
  totals      = data[:totals]
  stage_names = data[:stage_names]
  st          = totals["stageTimesNs"] || {}
  wall_ns     = totals["totalTimeNs"] || 0
  c_import_ns = data[:c_import_ns]
  out         = opts[:out]

  rows = [["Parse", totals["parseTimeNs"] || 0]] +
         stage_names.map { |s| [s, st[s] || 0] }
  rows << ["C Import", c_import_ns] if c_import_ns > 0

  name_w  = [rows.map { |n, _| n.length }.max, 8].max
  total_w = [rows.map { |_, ns| fmt_ns(ns).length }.max, 10].max
  row_w   = name_w + 3 + total_w + 3 + 6

  out.puts
  out.puts C.apply(C::BOLD, "  Compilation Summary")
  out.puts "  Files   : #{data[:total_files]}"
  out.puts "  Wall    : #{C.apply(C::BOLD, fmt_ns(wall_ns))}"
  out.puts
  out.puts divider(row_w + 2)
  out.puts "  %-*s | %*s | %s" % [name_w, C.apply(C::BOLD, "Stage"), total_w, "Time", "  %  "]
  out.puts divider(row_w + 2)

  rows.each do |name, ns|
    bar_ratio = wall_ns > 0 ? ns.to_f / wall_ns : 0
    bar_chars = (bar_ratio * 20).round
    bar = C.apply(C::heat(bar_ratio), "█" * bar_chars) + " " * (20 - bar_chars)
    out.puts "  %-*s | %*s | %s  %s" % [name_w, name, total_w, fmt_ns(ns), pct(ns, wall_ns), bar]
  end

  out.puts divider(row_w + 2)
  out.puts C.apply(C::BOLD, "  %-*s | %*s" % [name_w, "TOTAL", total_w, fmt_ns(wall_ns)])
  out.puts divider(row_w + 2)
  out.puts
end

# ── CLI ──────────────────────────────────────────────────────────────────────

options = {
  mode:      "table",
  output:    nil,
  bar_width: 50,
  no_colour: false,
}

OptionParser.new do |o|
  o.banner = "Usage: #{File.basename($0)} [profiling.json] [options]"
  o.on("--mode MODE",    "Output mode: table, heatmap, timeline, summary (default: table)") { |v| options[:mode]      = v }
  o.on("--output FILE",  "Write output to FILE instead of stdout")                          { |v| options[:output]    = v }
  o.on("--bar-width N",  Integer, "Bar width for timeline mode (default: 50)")              { |v| options[:bar_width] = v }
  o.on("--no-colour",    "Disable ANSI colour output")                                      {     options[:no_colour] = true }
  o.on("-h", "--help",   "Show this help")                                                  { puts o; exit }
end.parse!

C.enabled = false if options[:no_colour] || !$stdout.tty?

input_file = ARGV[0] || "profiling.json"
abort "File not found: #{input_file}" unless File.exist?(input_file)

data = load_profile(input_file)

out = options[:output] ? File.open(options[:output], "w") : $stdout
C.enabled = false if options[:output]  # no ANSI in files

opts = { out: out, bar_width: options[:bar_width] }

case options[:mode]
when "table"    then mode_table(data, opts)
when "heatmap"  then mode_heatmap(data, opts)
when "timeline" then mode_timeline(data, opts)
when "summary"  then mode_summary(data, opts)
else
  abort "Unknown mode '#{options[:mode]}'. Choose: table, heatmap, timeline, summary"
end

out.close if options[:output]
