/*
 *    Copyright (C) 2022 by YOUR NAME HERE
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

//#pragma push_macro("slots")
//#undef slots
//#include <Python.h>
//#pragma pop_macro("slots")

#ifndef SPECIFICWORKER_H
#define SPECIFICWORKER_H


#include <genericworker.h>
#include <Eigen/Dense>
#include <casadi/casadi.hpp>
#include <casadi/core/optistack.hpp>
#include <abstract_graphic_viewer/abstract_graphic_viewer.h>
#include "polypartition.h"


class SpecificWorker : public GenericWorker
{
Q_OBJECT
public:
    SpecificWorker(TuplePrx tprx, bool startup_check);

    ~SpecificWorker();

    bool setParams(RoboCompCommonBehavior::ParameterList params);

public slots:

    void compute();

    int startup_check();

    void initialize(int period);

    void new_target_slot(QPointF);

private:
    bool startup_check_flag;
    AbstractGraphicViewer *viewer_robot;
    void initialize_differential(const int N, const std::vector<double> &target_robot, const std::vector<double> &init_robot);
    std::vector<std::vector<double>> points_to_lines(const std::vector<Eigen::Vector2d> &points_in_robot);
    void move_robot(float adv, float rot, float side = 0);
    std::vector<double> e2v(const Eigen::Vector2d &v);
    QPointF e2q(const Eigen::Vector2d &v);
    Eigen::Vector2d from_robot_to_world(const Eigen::Vector2d &p, const Eigen::Vector2d &robot_tr, double robot_ang);
    Eigen::Vector2d from_world_to_robot(const Eigen::Vector2d &p, const Eigen::Vector2d &robot_tr, double robot_ang);
    void draw_path(const std::vector<double> &path,  const Eigen::Vector2d &tr_world, double robot_ang);

    Eigen::Vector2d target_in_world;
    bool active = true;
    casadi::Opti opti;
    int NUM_STEPS;
    casadi::MX state;
    casadi::MX pos;
    casadi::MX phi;
    casadi::MX control;
    casadi::MX adv;
    casadi::MX rot;
    casadi::MX target_oparam;
    casadi::MX initial_oparam;
    std::vector<casadi::MX> obs_lines;
    std::vector<Eigen::Vector2d> obs_points;

    //robot
    const int ROBOT_LENGTH = 400;
    QGraphicsPolygonItem *robot_polygon;
    QGraphicsRectItem *laser_in_robot_polygon;
    void draw_laser(const QPolygonF &poly_robot);
    std::tuple<QPolygonF, QPolygonF> read_laser(const Eigen::Vector2d &robot_tr, double robot_angle);

    // target
    struct Target
    {
        bool active = false;
        QPointF pos;
        Eigen::Vector2f to_eigen() const
        { return Eigen::Vector2f(pos.x(), pos.y()); }

        QGraphicsEllipseItem *draw = nullptr;
    };
    Target target;

    // convex parrtitions
    using Point = std::pair<float, float>;  //only for RDP, change to QPointF
    using Lines = std::vector<std::tuple<float, float, float>>;
    using Obstacles = std::vector<std::tuple<Lines, QPolygonF>>;
    std::vector<tuple<Lines, QPolygonF>> world_free_regions;

    Obstacles compute_laser_partitions(QPolygonF &laser_poly);
    QPolygonF ramer_douglas_peucker(const RoboCompLaser::TLaserData &ldata, double epsilon);
    void ramer_douglas_peucker_rec(const vector<Point> &pointList, double epsilon, std::vector<Point> &out);
    void draw_partitions(const Obstacles &obstacles, const QColor &color, bool print=false);
};
#endif
