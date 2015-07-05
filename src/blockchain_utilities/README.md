# Monero Blockchain Utilities

Copyright (c) 2014-2015, The Monero Project

## Introduction

For importing into the LMDB database, compile with `DATABASE=lmdb`

e.g.

`DATABASE=lmdb make release`

This is also the default compile setting on the blockchain branch.

By default, the exporter will use the original in-memory database (blockchain.bin) as its source.
This default is to make migrating to an LMDB database easy, without having to recompile anything.
To change the source, adjust `SOURCE_DB` in `src/blockchain_utilities/bootstrap_file.h` according to the comments.

## Usage:

See also each utility's "--help" option.

### Export an existing in-memory database

`$ blockchain_export`

This loads the existing blockchain, for whichever database type it was compiled for, and exports it to `$MONERO_DATA_DIR/export/blockchain.raw`

### Import the exported file

`$ blockchain_import`

This imports blocks from `$MONERO_DATA_DIR/export/blockchain.raw` into the current database.

Defaults: `--batch on`, `--batch size 20000`, `--verify on`

Batch size refers to number of blocks and can be adjusted for performance based on available RAM.

Verification should only be turned off if importing from a trusted blockchain.

```bash
## use default settings to import blockchain.raw into database
$ blockchain_import

## fast import with large batch size, verification off
$ blockchain_import --batch-size 100000 --verify off

## LMDB flags can be set by appending them to the database type:
## flags: nosync, nometasync, writemap, mapasync
$ blockchain_import --database lmdb#nosync
$ blockchain_import --database lmdb#nosync,nometasync
```

### Blockchain converter with batching
`blockchain_converter` has also been updated and includes batching for faster writes. However, on lower RAM systems, this will be slower than using the exporter and importer utilities. The converter needs to keep the blockchain in memory for the duration of the conversion, like the original bitmonerod, thus leaving less memory available to the destination database to operate.

```bash
$ blockchain_converter --batch on --batch-size 20000
```
