#ifndef TIMELOOP_H
#define TIMELOOP_H

#include <compile_time_options.h>

#include <discretization.h>
#include <offline_data.h>
#include <initial_values.h>
#include <postprocessor.h>
#include <time_step.h>

#include <deal.II/base/parameter_acceptor.h>
#include <deal.II/base/timer.h>

#include <sstream>

namespace ryujin
{

  template <int dim, typename Number = double>
  class TimeLoop : public dealii::ParameterAcceptor
  {
  public:
    using vector_type = typename grendel::TimeStep<dim, Number>::vector_type;

    TimeLoop(const MPI_Comm &mpi_comm);
    virtual ~TimeLoop() final = default;

    void run();

  private:
    /* Private methods for run(): */

    void initialize();

    vector_type interpolate_initial_values(Number t = 0);

    void compute_error(const vector_type &U, Number t);

    void output(const vector_type &U,
                const std::string &name,
                Number t,
                unsigned int cycle,
                bool checkpoint = false);

    void print_parameters();
    void print_mpi_partition();
    void print_head(const std::string &header,
                    const std::string &secondary = "",
                    bool use_cout = false);
    void print_cycle(unsigned int cycle, Number t, bool use_cout = false);
    void print_throughput(unsigned int cycle, Number t, bool use_cout = false);
    void print_cycle_statistics(unsigned int cycle,
                                Number t,
                                unsigned int output_cycle);

    /* Options: */

    std::string base_name;

    Number t_final;
    Number output_granularity;

    unsigned int update_granularity;

    bool enable_checkpointing;
    bool resume;

    bool write_mesh;
    bool write_output_files;

    bool enable_compute_error;

    /* Data: */

    const MPI_Comm &mpi_communicator;

    std::map<std::string, dealii::Timer> computing_timer;

    grendel::Discretization<dim> discretization;
    grendel::OfflineData<dim, Number> offline_data;
    grendel::InitialValues<dim, Number> initial_values;
    grendel::TimeStep<dim, Number> time_step;
    grendel::Postprocessor<dim, Number> postprocessor;

    const unsigned int mpi_rank;
    const unsigned int n_mpi_processes;

    std::unique_ptr<std::ofstream> filestream; /* log file */

    std::thread output_thread;
    unsigned int output_thread_active;
  };

} // namespace ryujin

#endif /* TIMELOOP_H */
