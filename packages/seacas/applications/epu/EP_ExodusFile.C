/*
 * Copyright(C) 1999-2021, 2023, 2024, 2025 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS).  Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 *
 * See packages/seacas/LICENSE for details
 */

#include "EP_ExodusFile.h"
#include "EP_Internals.h"
#include "EP_ParallelDisks.h"
#include "EP_SystemInterface.h"
#include "open_file_limit.h"
#include "smart_assert.h"

#include <fmt/color.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <climits>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#include <exodusII.h>

std::vector<int>         Excn::ExodusFile::fileids_;
std::vector<std::string> Excn::ExodusFile::filenames_;
int                      Excn::ExodusFile::processorCount_  = 0;
int                      Excn::ExodusFile::partCount_       = 0;
int                      Excn::ExodusFile::startPart_       = 0;
int                      Excn::ExodusFile::outputId_        = -1;
int                      Excn::ExodusFile::ioWordSize_      = 0;
int                      Excn::ExodusFile::cpuWordSize_     = 0;
int                      Excn::ExodusFile::mode64bit_       = 0;
int                      Excn::ExodusFile::changeSetCount_  = -1;
int                      Excn::ExodusFile::activeChangeSet_ = 0;
std::string              Excn::ExodusFile::outputFilename_;
bool                     Excn::ExodusFile::keepOpen_              = false;
bool                     Excn::ExodusFile::verifyValidFile_       = false;
bool                     Excn::ExodusFile::onlySelectedChangeSet_ = false;
int                      Excn::ExodusFile::maximumNameLength_     = 32;

namespace {
  void create_output_change_sets(const Excn::SystemInterface &si)
  {
    int save      = Excn::ExodusFile::set_active_change_set(0);
    int output_id = Excn::ExodusFile::output();

    Excn::ExodusFile::set_active_change_set(1);
    int               exoid             = Excn::ExodusFile(0);
    int               group_name_length = ex_inquire_int(exoid, EX_INQ_GROUP_NAME_LEN);
    std::vector<char> group_name(group_name_length + 1, '\0');

    fmt::print("Output file id = {}\n", output_id);
    for (int i = 0; i < Excn::ExodusFile::get_change_set_count(); i++) {
      int   idum = 0;
      float rdum = 0.0;
      // Get name of this group...
      int ierr = ex_inquire(exoid + i, EX_INQ_GROUP_NAME, &idum, &rdum, group_name.data());
      if (ierr != EX_NOERR) {
        throw std::runtime_error(fmt::format(
            "ERROR: (EPU) Could not get name for group {} in input file - exiting\n", i + 1));
      }
      int cs_id = ex_create_group(output_id, group_name.data());
      if (cs_id <= 0) {
        throw std::runtime_error(fmt::format(
            "ERROR: (EPU) Could not create group {} in output file.\n", fmt::join(group_name, "")));
      }
      if ((si.debug() & 1024) != 0) {
        fmt::print("Change set {} is {} with exoid {}\n", i + 1, group_name.data(), cs_id);
      }
    }
    Excn::ExodusFile::set_active_change_set(save);
  }
} // namespace

Excn::ExodusFile::ExodusFile(int processor) : myProcessor_(processor)
{
  SMART_ASSERT(processor < processorCount_)(processor)(processorCount_);
  SMART_ASSERT(fileids_.size() == static_cast<size_t>(processorCount_));
  if (!keepOpen_ && processor != 0) {
    float version          = 0.0;
    int   cpu_word_size    = cpuWordSize_;
    int   io_word_size_var = ioWordSize_;
    int   mode             = EX_READ;
    mode |= mode64bit_;

    fileids_[processor] =
        ex_open(filenames_[processor].c_str(), mode, &cpu_word_size, &io_word_size_var, &version);
    if (fileids_[processor] < 0) {
      throw std::runtime_error(
          fmt::format("ERROR: (EPU) Cannot open file '{}' - exiting\n", filenames_[processor]));
    }
    ex_set_max_name_length(fileids_[processor], maximumNameLength_);

    // Check for change_sets...
    set_change_set_count(fileids_[processor]);

    SMART_ASSERT(io_word_size_var == ioWordSize_);
    SMART_ASSERT(cpu_word_size == cpuWordSize_);
  }
}

void Excn::ExodusFile::set_change_set_count(int exoid)
{
  int num_change_sets = ex_inquire_int(exoid, EX_INQ_NUM_CHILD_GROUPS);
  if (changeSetCount_ < 0) {
    changeSetCount_ = num_change_sets;
    if (activeChangeSet_ >= changeSetCount_) {
      auto error =
          fmt::format("ERROR: (EPU) Selected Change set {} exceeds change set count {} - exiting\n",
                      activeChangeSet_ + 1, changeSetCount_);
      throw std::runtime_error(error);
    }
  }
  else {
    if (changeSetCount_ != num_change_sets) {
      throw std::runtime_error(
          "ERROR: (EPU) Inconsistent change set count in one or more files - exiting\n");
    }
  }
}

int Excn::ExodusFile::output()
{
  SMART_ASSERT(outputId_ >= 0);
  return outputId_ + (onlySelectedChangeSet_ ? 0 : activeChangeSet_);
}

Excn::ExodusFile::operator int() const
{
  SMART_ASSERT(fileids_[myProcessor_] >= 0);
  return fileids_[myProcessor_] + activeChangeSet_;
}

Excn::ExodusFile::~ExodusFile()
{
  try {
    if (!keepOpen_ && myProcessor_ != 0) {
      ex_close(fileids_[myProcessor_]);
      fileids_[myProcessor_] = -1;
    }
  }
  catch (...) {
  }
}

void Excn::ExodusFile::close_all()
{
  for (int p = 0; p < partCount_; p++) {
    if (fileids_[p] >= 0) {
      ex_close(fileids_[p]);
      fileids_[p] = -1;
    }
  }
  if (outputId_ >= 0) {
    ex_close(outputId_);
    outputId_ = -1;
  }

  // We have had some issues with file corruption.  Everything seems to be
  // working fine, but when the file is opened later after EPU has completed,
  // one or more of the output files (in subcyle mode) report being invalid.
  // Here, if the user requests, we try to reopen the file and report an
  // error if there is a problem.  This might still not catch all bad files,
  // but will hopefully catch some at the point that it happens...
  if (verifyValidFile_) {
    float version          = 0.0;
    int   cpu_word_size    = cpuWordSize_;
    int   io_word_size_var = ioWordSize_;
    int   mode             = EX_READ;
    mode |= mode64bit_;
    int exoid = ex_open(outputFilename_.c_str(), mode, &cpu_word_size, &io_word_size_var, &version);
    if (exoid < 0) {
      ex_get_err(nullptr, nullptr, &exoid);
      fmt::print(stderr, fmt::fg(fmt::color::red),
                 "EPU: Exodus error ({}) {}.\n"
                 "Output File verification failed for '{}'.  Could not reopen output file after "
                 "closing it\n",
                 exoid, ex_strerror(exoid), outputFilename_);
    }
    else {
      ex_close(exoid);
    }
  }
}

void Excn::ExodusFile::unlink_input_files()
{
  fmt::print("\n\tUnlinking {}\n\t  ...", filenames_[0]);
  for (int p = 0; p < partCount_; p++) {
    unlink(filenames_[p].c_str());
  }
  fmt::print("\n\tUnlinking {}\n\n", filenames_[partCount_ - 1]);
}

void Excn::ExodusFile::handle_temporary_files(bool delete_them)
{
  for (int p = 0; p < partCount_; p++) {
    if (fileids_[p] >= 0) {
      ex_close(fileids_[p]);
      if (delete_them) {
        unlink(filenames_[p].c_str());
      }
      fileids_[p] = -1;
    }
  }
  if (outputId_ >= 0) {
    ex_close(outputId_);
    outputId_ = -1;
  }
}

void Excn::ExodusFile::initialize(const SystemInterface &si, int start_part, int part_count,
                                  int cycle, bool joining_subcycle)
{
  processorCount_ = si.processor_count(); // Total number processors
  partCount_      = part_count;           // Total parts we are processing
  startPart_      = start_part;           // Which one to start with
  SMART_ASSERT(partCount_ + startPart_ <= processorCount_)(partCount_)(startPart_)(processorCount_);

  activeChangeSet_ = si.selected_change_set();
  if (si.selected_change_set() > 0) {
    onlySelectedChangeSet_ = true;
  }

  // EPU always wants entity (block, set, map) ids as 64-bit quantities...
  mode64bit_ = EX_IDS_INT64_API;
  if (si.int64()) {
    mode64bit_ |= EX_ALL_INT64_API;

    // For output...
    mode64bit_ |= EX_ALL_INT64_DB;
  }

  verifyValidFile_ = si.verify_valid_file();

  // See if we can keep files open
  int max_files = open_file_limit() - 1; // We also have an output exodus file.
  if (partCount_ <= max_files) {
    keepOpen_ = true;
    if (cycle == 0) {
      if ((si.debug() & 1) != 0) {
        fmt::print("Files kept open... (Max open = {})\n\n", max_files);
      }
    }
  }
  else {
    keepOpen_ = false;
    if (cycle == 0) {
      fmt::print("Single file mode... (Max open = {})\n"
                 "Consider using the -subcycle option for faster execution...\n\n",
                 max_files);
    }
  }

  fileids_.resize(processorCount_);
  filenames_.resize(processorCount_);

  std::string file_prefix   = si.basename();
  std::string exodus_suffix = si.exodus_suffix();

  std::string root_dir = si.root_dir();
  std::string sub_dir  = si.sub_dir();

  ParallelDisks disks;

  float version = 0.0;

  // create exo names
  for (int p = 0; p < partCount_; p++) {
    std::string name = file_prefix;
    if (!exodus_suffix.empty()) {
      name += "." + exodus_suffix;
    }

    int proc = p + startPart_;
    if (joining_subcycle) {
      name = si.output_filename();
      disks.rename_file_for_mp("", "", name, proc, processorCount_);
    }
    else {
      disks.rename_file_for_mp(root_dir, sub_dir, name, proc, processorCount_);
    }
    filenames_[p] = name;

    if (p == 0) {
      int cpu_word_size    = sizeof(float);
      int io_word_size_var = 0;
      int mode             = EX_READ;
      mode |= mode64bit_;
      int exoid = ex_open(filenames_[p].c_str(), mode, &cpu_word_size, &io_word_size_var, &version);
      if (exoid < 0) {
        fmt::print(stderr, fmt::fg(fmt::color::red), "Cannot open file '{}'\n", filenames_[p]);
        throw std::runtime_error("ERROR: (EPU) Problem initializing input and/or output files.\n");
      }

      // Check for change_sets...
      set_change_set_count(exoid);

      int int64db = ex_int64_status(exoid) & EX_ALL_INT64_DB;
      if (int64db != 0) {
        // If anything stored on input db as 64-bit int, then output db will have
        // everything stored as 64-bit ints and all API functions will use 64-bit
        mode64bit_ |= EX_ALL_INT64_API;
        mode64bit_ |= EX_ALL_INT64_DB;
      }

      int max_name = static_cast<int>(ex_inquire_int(exoid, EX_INQ_DB_MAX_USED_NAME_LENGTH));
      if (max_name > maximumNameLength_) {
        maximumNameLength_ = max_name;
      }

      ex_close(exoid);

      if (io_word_size_var < static_cast<int>(sizeof(float))) {
        io_word_size_var = sizeof(float);
      }

      ioWordSize_  = io_word_size_var;
      cpuWordSize_ = io_word_size_var;
    }

    if (keepOpen_ || p == 0) {
      int io_word_size_var = 0;
      int mode             = EX_READ;
      // All entity ids (block, sets) are read/written as 64-bit...
      mode |= mode64bit_;
      fileids_[p] =
          ex_open(filenames_[p].c_str(), mode, &cpuWordSize_, &io_word_size_var, &version);
      if (fileids_[p] < 0) {
        fmt::print(stderr, fmt::fg(fmt::color::red), "Cannot open file '{}'\n", filenames_[p]);
        throw std::runtime_error("ERROR: (EPU) Problem initializing input and/or output files.\n");
      }
      ex_set_max_name_length(fileids_[p], maximumNameLength_);
      SMART_ASSERT(ioWordSize_ == io_word_size_var)(ioWordSize_)(io_word_size_var);
    }

    if (((si.debug() & 64) != 0) || p == 0 || p == partCount_ - 1) {
      fmt::print("[{}] Input({}): '{}'\n", cycle, p, name);
      if (((si.debug() & 64) == 0) && p == 0) {
        fmt::print("...\n");
      }
    }
  }

  if ((mode64bit_ & EX_ALL_INT64_DB) != 0) {
    if (cycle == 0) {
      fmt::print("Input files contain 8-byte integers.\n");
    }
    si.set_int64();
  }
}

// `cs` is the 0-based change-set to select
int Excn::ExodusFile::set_active_change_set(int cs)
{
  int save = activeChangeSet_;

  if (changeSetCount_ < 0) {
    throw std::runtime_error(
        "ERROR: (EPU) Cannot set active change set before changes sets have been queried.\n");
  }
  if (cs <= changeSetCount_) {
    activeChangeSet_ = cs;
  }
  else {
    throw std::runtime_error(
        fmt::format("ERROR: (EPU) Active change set index {} exceeds change set count {}.\n", cs,
                    changeSetCount_));
  }
  return save;
}

void Excn::ExodusFile::create_output(const SystemInterface &si, int cycle)
// Create output file...
{
  std::string curdir        = si.cwd();
  std::string file_prefix   = si.basename();
  std::string output_suffix = si.output_suffix();

  outputFilename_ = std::move(file_prefix);
  if (!output_suffix.empty()) {
    outputFilename_ += "." + output_suffix;
  }

  if (!curdir.empty() && !Excn::is_path_absolute(outputFilename_)) {
    outputFilename_ = curdir + "/" + outputFilename_;
  }

  si.set_output_filename(outputFilename_);

  if (si.subcycle() > 1) {
    Excn::ParallelDisks::Create_IO_Filename(outputFilename_, cycle, si.subcycle());
  }

  // See if output file should be opened in netcdf4 format...
  // Did user specify it via -netcdf4 or -large_model argument...
  int mode = 0;

  if (changeSetCount_ > 1 && !onlySelectedChangeSet_) {
    mode |= EX_NETCDF4;
    mode |= EX_NOCLASSIC;
  }
  else if (si.compress_data() > 0 || si.szip()) {
    // Force netcdf-4 if compression is specified...
    mode |= EX_NETCDF4;
  }
  else if (si.use_netcdf4()) {
    mode |= EX_NETCDF4;
  }
  else if (si.use_netcdf5()) {
    mode |= EX_64BIT_DATA;
  }
  else if (ex_large_model(fileids_[0]) == 1) {
    mode |= EX_LARGE_MODEL;
  }

  mode |= mode64bit_;

  if (si.int64()) {
    mode |= EX_ALL_INT64_DB;
    mode |= EX_ALL_INT64_API;
  }

  if (si.append()) {
    fmt::print("[{}] Output:   '{}' (appending)\n", cycle, outputFilename_);
    float version = 0.0;
    mode |= EX_WRITE;
    outputId_ = ex_open(outputFilename_.c_str(), mode, &cpuWordSize_, &ioWordSize_, &version);
  }
  else {
    mode |= EX_CLOBBER;
    fmt::print("[{}] Output:   '{}'\n", cycle, outputFilename_);
    outputId_ = ex_create(outputFilename_.c_str(), mode, &cpuWordSize_, &ioWordSize_);
  }
  if (outputId_ < 0) {
    fmt::print(stderr, fmt::fg(fmt::color::red), "Cannot open file '{}'\n", outputFilename_);
    throw std::runtime_error("ERROR: (EPU) Problem creating output file.\n");
  }

  if (si.compress_data() > 0 || si.szip()) {
    if (si.szip()) {
      ex_set_option(outputId_, EX_OPT_COMPRESSION_TYPE, EX_COMPRESS_SZIP);
    }
    else if (si.zlib()) {
      ex_set_option(outputId_, EX_OPT_COMPRESSION_TYPE, EX_COMPRESS_ZLIB);
    }
    else if (si.zstd()) {
      ex_set_option(outputId_, EX_OPT_COMPRESSION_TYPE, EX_COMPRESS_ZSTD);
    }
    else if (si.bz2()) {
      ex_set_option(outputId_, EX_OPT_COMPRESSION_TYPE, EX_COMPRESS_BZ2);
    }
    ex_set_option(outputId_, EX_OPT_COMPRESSION_LEVEL, si.compress_data());
    ex_set_option(outputId_, EX_OPT_COMPRESSION_SHUFFLE, 1);

    if (si.quantize()) {
      ex_set_option(outputId_, EX_OPT_QUANTIZE_NSD, si.quantize_nsd());
    }
  }

  // Check whether input file(s) have changesets.  If they do and the `selected_change_set` is
  // not `0`, then we are combining all change sets on the input to the output.  Create them
  // now...
  if (changeSetCount_ > 1 && !onlySelectedChangeSet_) {
    // Get the names of the change sets on the input file and create them on output file...
    create_output_change_sets(si);
  }

  // EPU Can add a name of "processor_id_epu" which is 16 characters long.
  // Make sure maximumNameLength_ is at least that long...
  if (maximumNameLength_ < 16) {
    maximumNameLength_ = 16;
  }
  ex_set_option(outputId_, EX_OPT_MAX_NAME_LENGTH, maximumNameLength_);

  int int_size = si.int64() ? 8 : 4;
  if (cycle == 0) {
    fmt::print("IO Word sizes: {} bytes floating point and {} bytes integer.\n", ioWordSize_,
               int_size);
  }
}
