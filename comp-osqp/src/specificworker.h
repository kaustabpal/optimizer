/*
 *    Copyright (C) 2020 by YOUR NAME HERE
 *
 *    This file is part of RoboComp
 *
 *    RoboComp is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    RoboComp is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with RoboComp.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
	\brief
	@author authorname
*/



#ifndef SPECIFICWORKER_H
#define SPECIFICWORKER_H

#include <genericworker.h>
#include <innermodel/innermodel.h>
#include <QGraphicsScene>
#include <QGraphicsView>
#include "grid.cpp"
#include "grid.h"
#include "OsqpEigen/OsqpEigen.h"
#include <Eigen/Dense>
#include <doublebuffer/DoubleBuffer.h>
#include "qcustomplot.h"

class MyScene : public QGraphicsScene
{
    Q_OBJECT
    signals:
        void new_target(QGraphicsSceneMouseEvent *);
protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event)
    { emit new_target(event); }
};


class SpecificWorker : public GenericWorker
{
Q_OBJECT
public:
    SpecificWorker(TuplePrx tprx, bool startup_check);
    ~SpecificWorker();
    bool setParams(RoboCompCommonBehavior::ParameterList params);
    void JoystickAdapter_sendData (RoboCompJoystickAdapter::TData data);


public slots:

    void compute();
    int startup_check();
    void initialize(int period);

private:
    std::shared_ptr<InnerModel> innerModel;
    bool startup_check_flag;

    QPolygonF draw_laser(const RoboCompLaser::TLaserData &ldata);
    QPolygonF read_laser();
    RoboCompGenericBase::TBaseState read_base();

    // robot
    std::tuple<float, float, float> state_change(const RoboCompGenericBase::TBaseState &bState, float delta_t);
    const float ViriatoBase_WheelRadius = 76.2;
    const float ViriatoBase_DistAxes = 380.f;
    const float ViriatoBase_AxesLength = 422.f;
    const float ViriatoBase_Rotation_Factor = 8.1;  // it should be (DistAxes + AxesLength) / 2

    // target
    DoubleBuffer<Eigen::Vector2f, Eigen::Vector2f> target_buffer;

    // path
    std::vector<QGraphicsEllipseItem *> path_paint;
    QString path_color = "#FF00FF";
    bool atTarget = true;

    void draw_path(const std::vector<QPointF> &path);

    // Grid
    Grid<> grid;
    MyScene scene;
    QGraphicsItem *robot_polygon = nullptr;
    QGraphicsItem *laser_polygon = nullptr;
    const float ROBOT_LENGTH = 400;
    void fill_grid(const QPolygonF &ldata);

    // Draw
    QCustomPlot custom_plot;
    QCPGraph *xGraph, *yGraph, *wGraph;
    void init_drawing( Grid<>::Dimensions dim);
    float jadv = 0.0; float jrot = 0.0; float jside = 0.0;

    // optimizer
    constexpr static std::size_t state_dim = 3;
    constexpr static std::size_t control_dim = 3;

    using AMatrix = Eigen::Matrix<double, state_dim, state_dim>;
    using BMatrix = Eigen::Matrix<double, control_dim, control_dim>;
    using StateConstraintsMatrix = Eigen::Matrix<double, state_dim, 1>;
    using ControlConstraintsMatrix = Eigen::Matrix<double, control_dim, 1>;
    using QMatrix = Eigen::DiagonalMatrix<double, state_dim>;
    using RMatrix = Eigen::DiagonalMatrix<double, control_dim>;
    using StateSpaceMatrix = Eigen::Matrix<double, state_dim, 1>;
    using ControlSpaceVector = Eigen::Matrix<double, control_dim, 1>;

    const std::uint32_t horizon = 20;

    OsqpEigen::Solver solver;

    // controller input and QPSolution vector
    ControlSpaceVector ctr;
    Eigen::VectorXd QPSolution;

    // allocate the dynamics matrices
    AMatrix A;
    BMatrix B;

    // allocate the constraints vector
    StateConstraintsMatrix xMax;
    StateConstraintsMatrix  xMin;
    ControlConstraintsMatrix uMax;
    ControlConstraintsMatrix  uMin;

    // allocate the weight matrices
    QMatrix Q;
    RMatrix R;

    // allocate the initial and the reference state space
    StateSpaceMatrix x0;
    StateSpaceMatrix xRef;

    // allocate QP problem matrices and vectors
    Eigen::SparseMatrix<double> hessian;
    Eigen::VectorXd gradient;
    Eigen::SparseMatrix<double> linearMatrix;
    Eigen::VectorXd lowerBound;
    Eigen::VectorXd upperBound;

    void init_optmizer();
    void set_inequality_constraints(StateConstraintsMatrix &xMax, StateConstraintsMatrix&xMin,
                                    ControlConstraintsMatrix &uMax, ControlConstraintsMatrix &uMin,
                                    const ControlConstraintsMatrix &uzero);
    void set_weight_matrices(QMatrix &Q, RMatrix &R);
    void cast_MPC_to_QP_hessian(const QMatrix &Q, const RMatrix &R, int mpcWindow, Eigen::SparseMatrix<double> &hessianMatrix);
    void cast_MPC_to_QP_gradient(const QMatrix &Q, const StateSpaceMatrix &xRef, std::uint32_t horizon, Eigen::VectorXd &gradient);
    void update_constraint_vectors(const StateSpaceMatrix &x0, Eigen::VectorXd &lowerBound, Eigen::VectorXd &upperBound);
    void cast_MPC_to_QP_constraint_matrix(const AMatrix &dynamicMatrix, const BMatrix &controlMatrix, std::uint32_t horizon, Eigen::SparseMatrix<double> &constraintMatrix);
    void cast_MPC_to_QP_constraint_vectors(const StateConstraintsMatrix &xMax, const StateConstraintsMatrix &xMin,
                                      const ControlConstraintsMatrix &uMax, const ControlConstraintsMatrix &uMin,
                                      const StateSpaceMatrix &x0, std::uint32_t horizon, Eigen::VectorXd &lowerBound, Eigen::VectorXd &upperBound);
    double get_error_norm(const StateSpaceMatrix &x, const StateSpaceMatrix &xRef);
    void compute_jacobians(AMatrix &A, BMatrix &B, double u_x, double u_y, double alfa);
};


#endif
