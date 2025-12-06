/*
Copyright (C) 2012-2025 tim cotter. All rights reserved.
*/

/**
use levenberg-marquardt to find a set of parameters
that least squares best fits a set of results.
**/

#include <eigen3/Eigen/Dense>


class LevenbergMarquardt {
public:
    LevenbergMarquardt() = default;
    virtual ~LevenbergMarquardt() = default;

    /** must set these. **/
    int ndata_points_ = 0;
    int nparams_ = 0;

    /** set this to the initial guess. **/
    Eigen::VectorXd solution_;

    /** set this to the target values. **/
    Eigen::VectorXd targets_;

    /** verbosity level **/
    enum class Verbosity {
        kQuiet,
        kResultsOnly,
        kDetailedResults,
        kIterations,
        kDebug
    };
    Verbosity verbosity_ = Verbosity::kResultsOnly;

    /** tweaking these are optional. **/
    int max_error_iters_ = 100;
    int max_lambda_iters_ = 100;
    double init_lambda_ = 1.0;
    double epsilon_ = 0.0001;
    double lambda_inc_ = 2.0;
    double lambda_dec_ = 0.5;
    double good_error_ = 0.01;
    double min_error_change_ = 0.0001;

    /** outputs **/
    double error_ = 0.0;

    /** must implement this. **/
    virtual void makePrediction(
        const Eigen::VectorXd &solution,
        Eigen::VectorXd &predicted
    ) noexcept = 0;

    /** solve for the parameters. **/
    void solve() noexcept;

    /** internal functions. **/
    double calculateError(const Eigen::VectorXd &predicted) const noexcept;
    void calculateJacobian(Eigen::MatrixXd &jacobian, double epsilon) noexcept;
};
