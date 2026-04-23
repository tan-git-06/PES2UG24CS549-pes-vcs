from weasyprint import HTML

# Defining the content for the README.md and then converting it to a professional PDF report as well.
# This README is tailored for the "PES VCS" (Personal Version Control System) project.

readme_content = """# PES VCS: A Minimalist Version Control System

## Project Overview
PES VCS is a lightweight, Git-inspired version control system implemented in C. It demonstrates core operating system and filesystem concepts, including content-addressable storage, directory tree serialization, and atomic filesystem operations. The system allows users to initialize a repository, stage files, create commits, and view history.

## Core Features
* **Content-Addressable Storage:** Uses SHA-256 hashing to store blobs, trees, and commits.
* **Staging Area:** A text-based index file that tracks changes before they are committed.
* **Recursive Tree Building:** Converts a flat index into a hierarchical tree structure.
* **Atomic Operations:** Implements "write-to-temp-then-rename" patterns to ensure data integrity.
* **Commit History:** Linked-list style commit history traversal.

## Project Structure
* `object.c`: Handles the creation and retrieval of hashed objects (Blobs, Trees, Commits).
* `index.c`: Manages the staging area, including adding/removing files and status tracking.
* `tree.c`: Implements the recursive logic to build directory trees from the index.
* `commit.c`: Handles commit creation, metadata (author, timestamp), and HEAD updates.
* `pes.h / index.h / tree.h`: Interface definitions and shared data structures.

## Installation & Compilation
Ensure you have `gcc` and the OpenSSL development library installed (`libssl-dev` on Ubuntu).
