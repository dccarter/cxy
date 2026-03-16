/**
 * Copyright (c) 2026 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2026-03-15
 */

#include "args.h"

#include <stdio.h>
#include <string.h>

// Forward declarations
static void generateBashCompletion(CmdParser *parser, bool isMainParser, FILE *output);
static void generateZshCompletion(CmdParser *parser, bool isMainParser, FILE *output);

/**
 * Generate bash completion function name from parser name
 * e.g., "cxy" -> "_cxy", "cxy package" -> "_cxy_package"
 */
static void getBashFunctionName(const char *parserName, char *buffer, size_t bufSize)
{
    snprintf(buffer, bufSize, "_%s", parserName);
    for (char *p = buffer; *p; p++) {
        if (*p == ' ' || *p == '-') {
            *p = '_';
        }
    }
}

/**
 * Output all global flags for a parser (bash format)
 */
static void outputBashGlobalFlags(CmdParser *parser, FILE *output)
{
    for (u32 i = 0; i < parser->nargs; i++) {
        CmdFlag *flag = &parser->args[i];
        if (flag->name == NULL) continue;
        
        fprintf(output, "--%s ", flag->name);
        if (flag->sf != '\0') {
            fprintf(output, "-%c ", flag->sf);
        }
    }
}

/**
 * Output all command names for a parser (bash format)
 */
static void outputBashCommandNames(CmdParser *parser, FILE *output)
{
    for (u32 i = 0; i < parser->ncmds; i++) {
        fprintf(output, "%s ", parser->cmds[i]->name);
    }
}

/**
 * Output all flags for a specific command (bash format)
 */
static void outputBashCommandFlags(CmdCommand *cmd, FILE *output)
{
    for (u32 i = 0; i < cmd->nargs; i++) {
        CmdFlag *flag = &cmd->args[i];
        if (flag->name == NULL) continue;
        
        fprintf(output, "--%s ", flag->name);
        if (flag->sf != '\0') {
            fprintf(output, "-%c ", flag->sf);
        }
    }
}

/**
 * Generate inline bash completion for a subparser
 */
static void generateBashSubparserCompletion(CmdCommand *cmd, const char *shellType, FILE *output)
{
    char *argv[] = {(char*)cmd->name, "completion", (char*)shellType};
    
    if (cmd->parse != NULL) {
        cmd->parse(cmd->ctx, 3, argv);
    }
}

/**
 * Generate bash completion script
 */
static void generateBashCompletion(CmdParser *parser, bool isMainParser, FILE *output)
{
    if (!isMainParser) {
        // For subparsers, generate inline case statements
        fprintf(output, "            # Subcommands for %s\n", parser->name);
        fprintf(output, "            local subcmd=\"\"\n");
        fprintf(output, "            # Find subcommand\n");
        fprintf(output, "            for (( j=cmd_idx+1; j < ${#words[@]}-1; j++ )); do\n");
        fprintf(output, "                case \"${words[j]}\" in\n");
        fprintf(output, "                    -*) continue ;;\n");
        fprintf(output, "                    *)\n");
        fprintf(output, "                        subcmd=\"${words[j]}\"\n");
        fprintf(output, "                        break\n");
        fprintf(output, "                        ;;\n");
        fprintf(output, "                esac\n");
        fprintf(output, "            done\n\n");
        
        fprintf(output, "            if [[ -z \"$subcmd\" ]]; then\n");
        fprintf(output, "                # Complete subcommand names\n");
        fprintf(output, "                local subcmds=\"");
        outputBashCommandNames(parser, output);
        fprintf(output, "\"\n");
        fprintf(output, "                COMPREPLY=( $(compgen -W \"$subcmds\" -- \"$cur\") )\n");
        fprintf(output, "                return 0\n");
        fprintf(output, "            fi\n\n");
        
        fprintf(output, "            # Handle subcommand completion\n");
        fprintf(output, "            case \"$subcmd\" in\n");
        
        for (u32 i = 0; i < parser->ncmds; i++) {
            CmdCommand *cmd = parser->cmds[i];
            
            fprintf(output, "                %s)\n", cmd->name);
            
            // Include subparser's global flags + command flags
            fprintf(output, "                    if [[ \"$cur\" == -* ]]; then\n");
            fprintf(output, "                        local subcmd_flags=\"");
            outputBashGlobalFlags(parser, output);
            outputBashCommandFlags(cmd, output);
            fprintf(output, "\"\n");
            fprintf(output, "                        COMPREPLY=( $(compgen -W \"$subcmd_flags\" -- \"$cur\") )\n");
            
            if (cmd->npos > 0) {
                fprintf(output, "                    else\n");
                fprintf(output, "                        # Complete positional arguments\n");
                fprintf(output, "                        COMPREPLY=( $(compgen -f -- \"$cur\") )\n");
            }
            
            fprintf(output, "                    fi\n");
            
            fprintf(output, "                    ;;\n");
        }
        
        fprintf(output, "            esac\n");
        return;
    }

    // Generate full bash completion script for main parser
    char funcName[128];
    getBashFunctionName(parser->name, funcName, sizeof(funcName));
    
    fprintf(output, "#!/bin/bash\n");
    fprintf(output, "# Bash completion script for %s\n", parser->name);
    fprintf(output, "# Generated automatically from command parser structure\n\n");
    
    fprintf(output, "%s() {\n", funcName);
    fprintf(output, "    local cur prev words cword\n");
    fprintf(output, "    if type _init_completion &>/dev/null; then\n");
    fprintf(output, "        _init_completion || return\n");
    fprintf(output, "    else\n");
    fprintf(output, "        COMPREPLY=()\n");
    fprintf(output, "        cur=\"${COMP_WORDS[COMP_CWORD]}\"\n");
    fprintf(output, "        prev=\"${COMP_WORDS[COMP_CWORD-1]}\"\n");
    fprintf(output, "        words=(\"${COMP_WORDS[@]}\")\n");
    fprintf(output, "        cword=$COMP_CWORD\n");
    fprintf(output, "    fi\n\n");
    
    // Determine which command is being completed
    fprintf(output, "    # Find the command in the arguments\n");
    fprintf(output, "    local cmd=\"\"\n");
    fprintf(output, "    local cmd_idx=-1\n");
    fprintf(output, "    for (( i=1; i < ${#words[@]}-1; i++ )); do\n");
    fprintf(output, "        case \"${words[i]}\" in\n");
    fprintf(output, "            -*) continue ;;\n");
    fprintf(output, "            *)\n");
    fprintf(output, "                cmd=\"${words[i]}\"\n");
    fprintf(output, "                cmd_idx=$i\n");
    fprintf(output, "                break\n");
    fprintf(output, "                ;;\n");
    fprintf(output, "        esac\n");
    fprintf(output, "    done\n\n");
    
    // If no command selected yet, complete commands
    fprintf(output, "    # If no command found, complete command names\n");
    fprintf(output, "    if [[ -z \"$cmd\" ]]; then\n");
    fprintf(output, "        local commands=\"");
    outputBashCommandNames(parser, output);
    fprintf(output, "\"\n");
    fprintf(output, "        COMPREPLY=( $(compgen -W \"$commands\" -- \"$cur\") )\n");
    fprintf(output, "        return 0\n");
    fprintf(output, "    fi\n\n");
    
    // Handle per-command completion
    fprintf(output, "    # Complete based on the selected command\n");
    fprintf(output, "    case \"$cmd\" in\n");
    
    for (u32 i = 0; i < parser->ncmds; i++) {
        CmdCommand *cmd = parser->cmds[i];
        
        fprintf(output, "        %s)\n", cmd->name);
        
        // Check if this command has a subparser
        if (cmd->parse != NULL) {
            // Generate inline completion for subcommands
            generateBashSubparserCompletion(cmd, "bash", output);
        } else {
            // Normal command completion - include global flags + command flags
            fprintf(output, "            if [[ \"$cur\" == -* ]]; then\n");
            fprintf(output, "                local cmd_flags=\"");
            outputBashGlobalFlags(parser, output);
            outputBashCommandFlags(cmd, output);
            fprintf(output, "\"\n");
            fprintf(output, "                COMPREPLY=( $(compgen -W \"$cmd_flags\" -- \"$cur\") )\n");
            
            if (cmd->npos > 0) {
                fprintf(output, "            else\n");
                fprintf(output, "                # Complete positional arguments (files by default)\n");
                fprintf(output, "                COMPREPLY=( $(compgen -f -- \"$cur\") )\n");
            }
            
            fprintf(output, "            fi\n");
        }
        
        fprintf(output, "            ;;\n");
    }
    
    fprintf(output, "    esac\n");
    fprintf(output, "}\n\n");
    
    // Register the completion function
    const char *progName = parser->name;
    const char *space = strchr(progName, ' ');
    if (space) {
        char prog[64];
        size_t len = space - progName;
        if (len >= sizeof(prog)) len = sizeof(prog) - 1;
        strncpy(prog, progName, len);
        prog[len] = '\0';
        fprintf(output, "complete -F %s %s\n", funcName, prog);
    } else {
        fprintf(output, "complete -F %s %s\n", funcName, progName);
    }
}

/**
 * Escape special characters in help text for zsh completion
 */
static void outputEscapedHelp(const char *help, FILE *output)
{
    if (!help) return;
    
    for (const char *p = help; *p; p++) {
        switch (*p) {
            case '\'':
                fprintf(output, "'\\''");
                break;
            case '[':
            case ']':
            case ':':
            case '\\':
                fprintf(output, "\\%c", *p);
                break;
            case '\n':
                // Stop at newline - only use first line of help
                return;
            default:
                fputc(*p, output);
                break;
        }
    }
}

/**
 * Get the zsh function name prefix for a parser
 * e.g., "cxy" -> "_cxy", "cxy package" -> "_cxy_package"
 */
static void getZshFunctionPrefix(const char *parserName, char *buffer, size_t bufSize)
{
    snprintf(buffer, bufSize, "_%s", parserName);
    for (char *p = buffer; *p; p++) {
        if (*p == ' ' || *p == '-') {
            *p = '_';
        }
    }
}

/**
 * Output _arguments entries for a set of flags
 */
static void outputZshFlags(CmdFlag *flags, u32 nflags, FILE *output, bool *isFirst)
{
    for (u32 i = 0; i < nflags; i++) {
        CmdFlag *flag = &flags[i];
        if (flag->name == NULL) continue;
        
        if (!*isFirst) {
            fprintf(output, " \\\n");
        }
        *isFirst = false;
        
        fprintf(output, "    ");
        if (flag->sf != '\0') {
            fprintf(output, "'(-%c --%s)'{-%c,--%s}'[",
                    flag->sf, flag->name, flag->sf, flag->name);
        } else {
            fprintf(output, "'(--%s)'--%s'[", flag->name, flag->name);
        }
        outputEscapedHelp(flag->help, output);
        fprintf(output, "]'");
    }
}

/**
 * Generate a zsh completion function for a single command
 */
static void generateZshCommandFunction(const char *funcPrefix, 
                                        CmdCommand *cmd, 
                                        CmdFlag *globalFlags, 
                                        u32 nGlobalFlags,
                                        FILE *output)
{
    fprintf(output, "%s_%s() {\n", funcPrefix, cmd->name);
    fprintf(output, "  _arguments");
    
    bool isFirst = true;
    
    // Output global flags (inherited from parser)
    outputZshFlags(globalFlags, nGlobalFlags, output, &isFirst);
    
    // Output command-specific flags
    outputZshFlags(cmd->args, cmd->nargs, output, &isFirst);
    
    // Output positional argument handling
    if (cmd->npos > 0) {
        if (!isFirst) {
            fprintf(output, " \\\n");
        }
        fprintf(output, "    '*:files:_files'");
    }
    
    fprintf(output, "\n}\n\n");
}

/**
 * Generate zsh completion function for a subparser command
 * This generates both the dispatcher and individual command functions
 */
static void generateZshSubparserFunction(CmdCommand *cmd, const char *shellType, FILE *output)
{
    // Invoke the subparser with synthetic argv to generate its functions
    char *argv[] = {(char*)cmd->name, "completion", (char*)shellType};
    
    if (cmd->parse != NULL) {
        cmd->parse(cmd->ctx, 3, argv);
    }
}

/**
 * Generate zsh completion script
 */
static void generateZshCompletion(CmdParser *parser, bool isMainParser, FILE *output)
{
    char funcPrefix[128];
    getZshFunctionPrefix(parser->name, funcPrefix, sizeof(funcPrefix));
    
    // Extract the program name (first word of parser->name)
    char prog[64];
    const char *space = strchr(parser->name, ' ');
    if (space) {
        size_t len = space - parser->name;
        if (len >= sizeof(prog)) len = sizeof(prog) - 1;
        strncpy(prog, parser->name, len);
        prog[len] = '\0';
    } else {
        strncpy(prog, parser->name, sizeof(prog) - 1);
        prog[sizeof(prog) - 1] = '\0';
    }
    
    if (isMainParser) {
        // Generate the header
        fprintf(output, "#compdef %s\n", prog);
        fprintf(output, "# Zsh completion script for %s\n", parser->name);
        fprintf(output, "# Generated automatically from command parser structure\n\n");
    }
    
    // First, generate functions for commands with subparsers
    for (u32 i = 0; i < parser->ncmds; i++) {
        CmdCommand *cmd = parser->cmds[i];
        if (cmd->parse != NULL) {
            generateZshSubparserFunction(cmd, "zsh", output);
        }
    }
    
    // Generate individual command functions (for commands without subparsers)
    for (u32 i = 0; i < parser->ncmds; i++) {
        CmdCommand *cmd = parser->cmds[i];
        if (cmd->parse == NULL) {
            generateZshCommandFunction(funcPrefix, cmd, parser->args, parser->nargs, output);
        }
    }
    
    // Generate the main dispatcher function
    fprintf(output, "%s() {\n", funcPrefix);
    fprintf(output, "  local curcontext=\"$curcontext\" state line ret=1\n");
    fprintf(output, "  typeset -A opt_args\n\n");
    
    fprintf(output, "  _arguments -C \\\n");
    fprintf(output, "    '1:command:->command' \\\n");
    fprintf(output, "    '*::options:->options' && return 0\n\n");
    
    fprintf(output, "  case \"$state\" in\n");
    fprintf(output, "    command)\n");
    fprintf(output, "      local -a commands\n");
    fprintf(output, "      commands=(\n");
    
    // List all commands with descriptions
    for (u32 i = 0; i < parser->ncmds; i++) {
        CmdCommand *cmd = parser->cmds[i];
        fprintf(output, "        '%s:", cmd->name);
        outputEscapedHelp(cmd->help, output);
        fprintf(output, "'\n");
    }
    
    fprintf(output, "      )\n");
    fprintf(output, "      _describe -t commands 'command' commands && ret=0\n");
    fprintf(output, "      ;;\n");
    
    fprintf(output, "    options)\n");
    fprintf(output, "      local command=\"$line[1]\"\n");
    fprintf(output, "      curcontext=\"${curcontext%%:*}:%s-$command:\"\n\n", prog);
    
    fprintf(output, "      # Dispatch to command-specific completion function\n");
    fprintf(output, "      local completion_func=\"%s_$command\"\n", funcPrefix);
    fprintf(output, "      if (( $+functions[$completion_func] )); then\n");
    fprintf(output, "        $completion_func && ret=0\n");
    fprintf(output, "      fi\n");
    fprintf(output, "      ;;\n");
    
    fprintf(output, "  esac\n");
    fprintf(output, "  return ret\n");
    fprintf(output, "}\n\n");
    
    if (isMainParser) {
        fprintf(output, "%s \"$@\"\n", funcPrefix);
    }
}

/**
 * Main entry point for generating shell completions
 */
bool cmdGenerateCompletion(CmdParser *parser, const char *shellType, bool isMainParser)
{
    FILE *output = stdout;
    
    if (strcmp(shellType, "bash") == 0) {
        generateBashCompletion(parser, isMainParser, output);
        return true;
    } else if (strcmp(shellType, "zsh") == 0) {
        generateZshCompletion(parser, isMainParser, output);
        return true;
    }
    
    fprintf(stderr, "Unknown shell type: %s (supported: bash, zsh)\n", shellType);
    return false;
}