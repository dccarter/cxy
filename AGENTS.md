# AI Agent Instructions for Cxy Programming Language

## CMake Project Overview

Cxy is a modern compiled programming language implemented in C. This project uses a carefully designed architecture with arena-based memory management, comprehensive diagnostic reporting, and a multi-phase parser development approach.

## Development Philosophy

### Incremental Development

- **Feature development must be incremental and unit-testable**
- Each component should be implementable and testable in isolation
- New features require corresponding test cases before implementation
- Avoid large monolithic changes - break work into small, verifiable steps

### Building
- Configure project with CMake and build with `make -j $(nproc)`
