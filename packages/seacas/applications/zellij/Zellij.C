// Copyright(C) 2021 National Technology & Engineering Solutions
// of Sandia, LLC (NTESS).  Under the terms of Contract DE-NA0003525 with
// NTESS, the U.S. Government retains certain rights in this software.
//
// See packages/seacas/LICENSE for details
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#ifndef _MSC_VER
#include <sys/times.h>
#include <sys/utsname.h>
#endif
#include <unistd.h>
#include <vector>

#include "add_to_log.h"
#include "fmt/chrono.h"
#include "fmt/color.h"
#include "fmt/ostream.h"
#include "tokenize.h"

#include <exodusII.h>

#include <Ionit_Initializer.h>
#include <Ioss_SmartAssert.h>
#include <Ioss_SubSystem.h>
#include <Ioss_Transform.h>

#include "Cell.h"
#include "Grid.h"
#include "UnitCell.h"
#include "ZE_SystemInterface.h"

#ifdef SEACAS_HAVE_MPI
#include <mpi.h>
#endif

namespace {
  int get_free_descriptor_count()
  {
// Returns maximum number of files that one process can have open
// at one time. (POSIX)
#if defined(_WIN32)
    int fdmax = _getmaxstdio();
#else
    int fdmax = sysconf(_SC_OPEN_MAX);
    if (fdmax == -1) {
      // POSIX indication that there is no limit on open files...
      fdmax = INT_MAX;
    }
#endif
    // File descriptors are assigned in order (0,1,2,3,...) on a per-process
    // basis.

    // Assume that we have stdin, stdout, stderr files (3 total).

    return fdmax - 3;
  }

  Grid define_lattice(UnitCellMap &unit_cells, SystemInterface &interFace, Ioss::ParallelUtils &pu);

  std::string time_stamp(const std::string &format);
} // namespace

std::string  tsFormat    = "[{:%H:%M:%S}]";
unsigned int debug_level = 0;

template <typename INT> double zellij(SystemInterface &interFace, INT dummy);

int main(int argc, char *argv[])
{
#ifdef SEACAS_HAVE_MPI
  MPI_Init(&argc, &argv);

#endif

  try {
    SystemInterface::show_version();
#ifdef SEACAS_HAVE_MPI
    fmt::print("\tParallel Capability Enabled.\n");
#else
    fmt::print("\tParallel Capability Not Enabled.\n");
#endif
    Ioss::Init::Initializer io;

    SystemInterface interFace;
    bool            ok = interFace.parse_options(argc, argv);

    if (!ok) {
      fmt::print(stderr, fmt::fg(fmt::color::red),
                 "\nERROR: Problems parsing command line arguments.\n\n");
      exit(EXIT_FAILURE);
    }

    double time = 0.0;
    if (interFace.ints32bit()) {
      time = zellij(interFace, 0);
    }
    else {
      time = zellij(interFace, static_cast<int64_t>(0));
    }
    fmt::print(stderr, "\n Zellij execution successful.\n");

    add_to_log(argv[0], time);

#ifdef SEACAS_HAVE_MPI
    MPI_Finalize();
#endif
  }
  catch (std::exception &e) {
    fmt::print(stderr, fmt::fg(fmt::color::red), "ERROR: Standard exception: {}\n", e.what());
  }
}

template <typename INT> double zellij(SystemInterface &interFace, INT /*dummy*/)
{
  double              begin = Ioss::Utils::timer();
  Ioss::ParallelUtils pu{MPI_COMM_WORLD};

  debug_level = interFace.debug();

  if ((debug_level & 8) != 0U) {
    ex_opts(EX_VERBOSE | EX_DEBUG);
  }
  else {
    ex_opts(0);
  }

  if (debug_level & 1) {
    fmt::print(stderr, "{} Begin Execution\n", time_stamp(tsFormat));
  }

  UnitCellMap unit_cells;
  auto        grid = define_lattice(unit_cells, interFace, pu);

  if (debug_level & 1) {
    fmt::print(stderr, "{} Lattice Defined\n", time_stamp(tsFormat));
  }

  grid.set_coordinate_offsets();

  grid.decompose(interFace.ranks(), interFace.decomp_method());
  if (debug_level & 1) {
    fmt::print(stderr, "{} Lattice Decomposed\n", time_stamp(tsFormat));
  }

  // All unit cells have been mapped into the IxJ grid, now calculate all node / element offsets
  // and the global node and element counts... (TODO: Parallel decomposition)
  //
  // Iterate through the grid starting with (0,0) and accumulate node and element counts...
  int start_rank = interFace.start_rank();
  int rank_count = interFace.rank_count();
  grid.finalize(start_rank, rank_count);
  if (debug_level & 1) {
    fmt::print(stderr, "{} Lattice Finalized\n", time_stamp(tsFormat));
  }

  grid.output_model(start_rank, rank_count, (INT)0);
  if (debug_level & 1) {
    fmt::print(stderr, "{} Model Output\n", time_stamp(tsFormat));
  }

  /*************************************************************************/
  // EXIT program
  if (debug_level & 1) {
    fmt::print(stderr, "{} Execution Complete\n", time_stamp(tsFormat));
  }

  double end = Ioss::Utils::timer();
  fmt::print(stderr, "\n Total Execution time     = {:.5} seconds.\n", end - begin);
  double hwm = (double)Ioss::Utils::get_hwm_memory_info() / 1024.0 / 1024.0;
  fmt::print(stderr, " High-Water Memory Use    = {:.3} MiBytes.\n", hwm);
  return (end - begin);
}

namespace {
  std::string time_stamp(const std::string &format)
  {
    if (format == "") {
      return std::string("");
    }

    time_t      calendar_time = std::time(nullptr);
    struct tm * local_time    = std::localtime(&calendar_time);
    std::string time_string   = fmt::format(format, *local_time);
    return time_string;
  }

  std::shared_ptr<Ioss::Region> create_input_region(const std::string &key, std::string filename,
                                                    bool ints_32_bits)
  {
    // Check that 'filename' does not contain a starting/ending double quote...
    filename.erase(remove(filename.begin(), filename.end(), '\"'), filename.end());
    Ioss::DatabaseIO *dbi =
        Ioss::IOFactory::create("exodus", filename, Ioss::READ_RESTART, (MPI_Comm)MPI_COMM_SELF);
    if (dbi == nullptr || !dbi->ok(true)) {
      std::exit(EXIT_FAILURE);
    }

    dbi->set_surface_split_type(Ioss::SPLIT_BY_DONT_SPLIT);
    if (ints_32_bits) {
      dbi->set_int_byte_size_api(Ioss::USE_INT32_API);
    }
    else {
      dbi->set_int_byte_size_api(Ioss::USE_INT64_API);
    }

    // Splitting surfaces by element block makes it easier to transform the input
    // element id into the output element ids.
    dbi->set_surface_split_type(Ioss::SPLIT_BY_ELEMENT_BLOCK);

    // Generate a name for the region based on the key...
    std::string name = "Region_" + key;
    // NOTE: region owns database pointer at this time...
    return std::make_shared<Ioss::Region>(dbi, name);
  }

  Grid define_lattice(UnitCellMap &unit_cells, SystemInterface &interFace, Ioss::ParallelUtils &pu)
  {
    if (debug_level & 2) {
      pu.progress("Defining Unit Cells...");
    }
    std::string filename = interFace.lattice();

    std::ifstream input(filename, std::ios::in);
    if (!input.is_open()) {
      fmt::print(stderr, fmt::fg(fmt::color::red),
                 "\nERROR: Could not open/access lattice file '{}'\n", filename);
      exit(EXIT_FAILURE);
    }

    bool        in_dictionary{false};
    bool        in_lattice{false};
    std::string line;
    while (getline(input, line)) {
      if (line.empty()) {
        continue;
      }

      auto tokens = Ioss::tokenize(line, " ");
      if (tokens[0] == "BEGIN_DICTIONARY") {
        SMART_ASSERT(!in_lattice && !in_dictionary);
        in_dictionary = true;
      }
      else if (tokens[0] == "END_DICTIONARY") {
        SMART_ASSERT(!in_lattice && in_dictionary);
        in_dictionary = false;
        if (debug_level & 2) {
          pu.progress("Unit Cells Defined...");
        }
      }
      else if (in_dictionary) {
        if (tokens.size() != 2) {
          fmt::print(stderr, fmt::fg(fmt::color::red),
                     "\nERROR: There are {} entries on a lattice dictionary line; there should be "
                     "only 2:\n\t'{}'.\n\n",
                     tokens.size(), line);
          exit(EXIT_FAILURE);
        }
        if (unit_cells.find(tokens[0]) != unit_cells.end()) {
          fmt::print(
              stderr, fmt::fg(fmt::color::red),
              "\nERROR: There is a duplicate `unit cell` ({}) in the lattice dictionary.\n\n",
              tokens[0]);
          exit(EXIT_FAILURE);
        }

        auto region = create_input_region(tokens[0], tokens[1], interFace.ints32bit());

        if (region == nullptr) {
          fmt::print(
              stderr, fmt::fg(fmt::color::red),
              "\nERROR: Unable to open the database '{}' associated with the unit cell '{}'.\n\n",
              tokens[1], tokens[0]);
          exit(EXIT_FAILURE);
        }

        unit_cells.emplace(tokens[0], std::make_shared<UnitCell>(region));
        if (interFace.minimize_open_files() == Minimize::UNIT ||
            interFace.minimize_open_files() == Minimize::ALL) {
          unit_cells[tokens[0]]->m_region->get_database()->closeDatabase();
        }

        if (debug_level & 2) {
          pu.progress(fmt::format("\tCreated Unit Cell {}", tokens[0]));
        }
      }
      else if (tokens[0] == "BEGIN_LATTICE") {
        SMART_ASSERT(!in_lattice && !in_dictionary);
        in_lattice = true;
        break;
      }
    }

    if (!in_lattice) {
      // ERROR -- file ended before lattice definition...
    }

    // Tokenize line to get I J K size of lattice
    auto tokens = Ioss::tokenize(line, " ");
    SMART_ASSERT(tokens[0] == "BEGIN_LATTICE")(tokens[0])(line);

    if (tokens.size() != 4) {
      fmt::print(stderr, fmt::fg(fmt::color::red),
                 "\nERROR: The 'BEGIN_LATTICE' line has incorrect syntax.  It should be "
                 "'BEGIN_LATTICE I J K'\n"
                 "\tThe line was '{}'\n\n",
                 line);
      exit(EXIT_FAILURE);
    }

    SMART_ASSERT(tokens.size() == 4)(tokens.size())(line);
    int II = std::stoi(tokens[1]);
    int JJ = std::stoi(tokens[2]);
    int KK = std::stoi(tokens[3]);
    SMART_ASSERT(KK == 1);

    Grid grid(interFace, II, JJ);

    size_t open_files = get_free_descriptor_count();
    fmt::print("\n Maximum Open File Count = {}\n", get_free_descriptor_count());
    if (interFace.minimize_open_files() != Minimize::NONE ||
        (unit_cells.size() + interFace.rank_count() > open_files)) {
      unsigned int mode = (unsigned)interFace.minimize_open_files();
      if (unit_cells.size() + interFace.rank_count() > open_files) {
        if (unit_cells.size() > (size_t)interFace.rank_count()) {
          mode |= 1;
        }
        else {
          mode |= 2;
        }
      }
      if (unit_cells.size() > open_files) {
        mode |= 1;
      }
      if ((size_t)interFace.rank_count() > open_files) {
        mode |= 2;
      }
      grid.set_minimize_open_files(mode);
      std::array<std::string, 4> smode{"NONE", "UNIT", "OUTPUT", "ALL"};
      fmt::print(stderr, " Setting `minimize_open_files` mode to {}.\n", smode[mode]);
    }

    fmt::print(stderr, "\n Lattice:\tUnit Cells: {:n},\tGrid Size:  {:n} x {:n} x {:n}\n",
               unit_cells.size(), II, JJ, KK);
    if (interFace.ranks() > 1) {
      fmt::print(stderr, "         \tRanks: {:n}, Outputting {:n} ranks starting at rank {:n}.\n",
                 interFace.ranks(), interFace.rank_count(), interFace.start_rank());
    }

    size_t row{0};
    while (getline(input, line)) {
      if (line.empty()) {
        continue;
      }

      tokens = Ioss::tokenize(line, " ");
      if (tokens[0] == "END_LATTICE") {
        SMART_ASSERT(in_lattice && !in_dictionary);
        in_lattice = false;

        // Check row count to make sure matches 'J' size of lattice
        if (row != grid.JJ()) {
          fmt::print(stderr, fmt::fg(fmt::color::red),
                     "\nERROR: Only {} rows of the {} x {} lattice were defined.\n\n", row, II, JJ);
          exit(EXIT_FAILURE);
        }
        break;
      }
      else if (in_lattice) {
        // TODO: Currently assumes that each row in the lattice is defined on a single row;
        //       This will need to be relaxed since a lattice of 5000x5000 would result in
        //       lines that are too long and would be easier to split a row over multiple lines...
        if (tokens.size() != grid.II()) {
          fmt::print(
              stderr, fmt::fg(fmt::color::red),
              "\nERROR: Line {} of the lattice definition has {} entries.  It should have {}.\n\n",
              row + 1, tokens.size(), grid.II());
          exit(EXIT_FAILURE);
        }

        if (row >= grid.JJ()) {
          fmt::print(stderr, fmt::fg(fmt::color::red),
                     "\nERROR: There are too many rows in the lattice definition. The lattice is "
                     "{} x {}.\n\n",
                     grid.II(), grid.JJ());
          exit(EXIT_FAILURE);
        }

        if (tokens.size() != grid.II()) {
          fmt::print(stderr, fmt::fg(fmt::color::red),
                     "\nERROR: In row {}, there is an incorrect number of entries.  There should "
                     "be {}, but found {}.\n",
                     row + 1, grid.II(), tokens.size());
          exit(EXIT_FAILURE);
        }

        size_t col = 0;
        for (auto &key : tokens) {
          if (unit_cells.find(key) == unit_cells.end()) {
            fmt::print(stderr, fmt::fg(fmt::color::red),
                       "\nERROR: In row {}, column {}, the lattice specifies a unit cell ({}) that "
                       "has not been defined.\n\n",
                       row + 1, col + 1, key);
            exit(EXIT_FAILURE);
          }

          auto &unit_cell = unit_cells[key];
          SMART_ASSERT(unit_cell->m_region != nullptr)(row)(col)(key);
          grid.initialize(col++, row, unit_cell);
        }
        row++;
      }
    }
    return grid;
  }
} // namespace
