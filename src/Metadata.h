#ifndef MNEMOSYNE_METADATA_H
#define MNEMOSYNE_METADATA_H

#include <openssl/sha.h>

#include <fstream>
#include "Chunk.h"
#include "utility/hash.cpp"

#define BUFFER_LEN 4096

#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
using namespace std;

string locationMetadata(const char *name, fs::path path_dir);
bool buildMetadata(const char *name, char* digest, vector<Chunk*>& chunks, fs::path path_dir);
bool extractChunks(const char *name, char* digest, vector<Chunk*>& chunks, fs::path path_dir);
#endif
