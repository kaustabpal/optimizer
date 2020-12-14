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
#include "specificworker.h"
#include <cppitertools/enumerate.hpp>
#include <ranges>

/**
* \brief Default constructor
*/
SpecificWorker::SpecificWorker(TuplePrx tprx, bool startup_check) : GenericWorker(tprx)
{
	this->startup_check_flag = startup_check;
    readSettings();
}

/**
* \brief Default destructor
*/
SpecificWorker::~SpecificWorker()
{
    //delete env;
	std::cout << "Destroying SpecificWorker" << std::endl;
}

bool SpecificWorker::setParams(RoboCompCommonBehavior::ParameterList params)
{
	try
	{
		RoboCompCommonBehavior::Parameter par = params.at("InnerModelPath");
		std::string innermodel_path = par.value;
		innerModel = std::make_shared<InnerModel>(innermodel_path);

	}
	catch(const std::exception &e) { qFatal("Error reading config params"); }
	return true;
}

void SpecificWorker::initialize(int period)
{
	std::cout << "Initialize worker" << std::endl;

	//grid
	Grid<>::Dimensions dim;  //default values
    grid.initialize(&scene, dim);

    //view
    init_drawing(dim);

    // model
    auto laser_poly = read_laser();
    auto obstacles = compute_laser_partitions(laser_poly);
    initialize_model(StateVector(0,0,0), obstacles);

	this->Period = period;
	if(this->startup_check_flag)
		this->startup_check();
	else
		timer.start(Period);
}


void SpecificWorker::compute()
{
    auto bState = read_base();
    auto laser_poly = read_laser();  // returns poly in robot coordinates
    draw_laser(laser_poly);
    auto obstacles = compute_laser_partitions(laser_poly);
    draw_partitions(obstacles, false);

    // fill_grid(laser_poly);

    // check for new target
    if(auto t = target_buffer.try_get(); t.has_value())
    {
        //set target
        target.setX(t.value().x()); target.setY(t.value().y());

        // draw
        draw_target(bState, t.value());
    }
    if(not atTarget)
    {
        QVec rtarget =  innerModel->transform("base", QVec::vec3(target.x(), 0., target.y()), "world");
        double target_ang = -atan2(rtarget[0], rtarget[2]);
        double rot_error = -atan2(target.x() - bState.x, target.y() - bState.z) - bState.alpha;
        double pos_error = rtarget.norm2();

        if (pos_error < 40)
            stop_robot();
        else
        {
            optimize(StateVector(rtarget.x(), rtarget.z(), target_ang), obstacles);
            int status = model->get(GRB_IntAttr_Status);
            if(status != GRB_OPTIMAL)
            {
                qInfo() << __FUNCTION__ << "Result status:" << status << " Aborting.";
                std::terminate();
            }
            float x = control_vars[0].get(GRB_DoubleAttr_X);
            float y = control_vars[1].get(GRB_DoubleAttr_X);
            float a = control_vars[2].get(GRB_DoubleAttr_X);
            omnirobot_proxy->setSpeedBase(x, y, a);

            // draw
            draw(ControlVector(x,y,a), pos_error, rot_error);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////

void SpecificWorker::initialize_model(const StateVector &target, const Obstacles &obstacles)
{
    // Create environment
    //env = new GRBEnv("path_optimization.log");
    //env.set("LogFile", "path_optimization.log");

    // Create initial model
    model = new GRBModel(env);
    model->set(GRB_StringAttr_ModelName, "path_optimization");
    model_vars = model->addVars((STATE_DIM + CONTROL_DIM) * NUM_STEPS, GRB_CONTINUOUS);
    state_vars = &(model_vars[0]);
    control_vars = &(model_vars[STATE_DIM * NUM_STEPS]);
    //sin_cos_vars = &(model_vars[4*NUM_STEPS]);

    for (uint e = 0; e < NUM_STEPS; e++)
    {
        ostringstream v_name_x, v_name_y, v_name_ang;
        v_name_x << "x" << e;
        state_vars[e * STATE_DIM].set(GRB_StringAttr_VarName, v_name_x.str());
        state_vars[e * STATE_DIM].set(GRB_DoubleAttr_LB, -10000);
        state_vars[e * STATE_DIM].set(GRB_DoubleAttr_UB, +10000);
        //state_vars[e*2].set(GRB_DoubleAttr_Start, path[e].x());
        v_name_y << "y" << e;
        state_vars[e * STATE_DIM + 1].set(GRB_StringAttr_VarName, v_name_y.str());
        state_vars[e * STATE_DIM + 1].set(GRB_DoubleAttr_LB, -10000);
        state_vars[e * STATE_DIM + 1].set(GRB_DoubleAttr_UB, +10000);
        v_name_ang << "a" << e;
        state_vars[e * STATE_DIM + 2].set(GRB_StringAttr_VarName, v_name_ang.str());
        state_vars[e * STATE_DIM + 2].set(GRB_DoubleAttr_LB, -M_PI);
        state_vars[e * STATE_DIM + 2].set(GRB_DoubleAttr_UB, M_PI);

        //state_vars[e*2+1].set(GRB_DoubleAttr_Start, path[e].y());
    }
    for (uint e = 0; e < NUM_STEPS; e++)
    {
        ostringstream v_name_u, v_name_v, v_name_w, vnamesin, vnamecos;
        v_name_u << "u" << e;
        control_vars[e * CONTROL_DIM].set(GRB_StringAttr_VarName, v_name_u.str());
        control_vars[e * CONTROL_DIM].set(GRB_DoubleAttr_LB, -10000);
        control_vars[e * CONTROL_DIM].set(GRB_DoubleAttr_UB, +10000);

        v_name_v << "v" << e;
        control_vars[e * CONTROL_DIM + 1].set(GRB_StringAttr_VarName, v_name_v.str());
        control_vars[e * CONTROL_DIM + 1].set(GRB_DoubleAttr_LB, -10000);
        control_vars[e * CONTROL_DIM + 1].set(GRB_DoubleAttr_UB, +10000);

        v_name_w << "w" << e;
        control_vars[e * CONTROL_DIM + 2].set(GRB_StringAttr_VarName, v_name_w.str());
        control_vars[e * CONTROL_DIM + 2].set(GRB_DoubleAttr_LB, -M_PI);
        control_vars[e * CONTROL_DIM + 2].set(GRB_DoubleAttr_UB, M_PI);

        // vnamesin << "sin_v" << e;
        // sin_cos_vars[e*2].set(GRB_StringAttr_VarName, vnamesin.str());
        // vnamecos << "cos_v" << e;
        // sin_cos_vars[e*2+1].set(GRB_StringAttr_VarName, vnamecos.str());

    }

    // initial state value
    model->addConstr(state_vars[0] == 0, "c0x");
    model->addConstr(state_vars[1] == 0, "c0y");
    model->addConstr(state_vars[2] == 0, "c0a");
    // final state should be equal to target
    model->addConstr(state_vars[(NUM_STEPS - 1) * STATE_DIM] == target.x(), "c1x");
    model->addConstr(state_vars[(NUM_STEPS - 1) * STATE_DIM + 1] == target.y(), "c1y");
    model->addConstr(state_vars[(NUM_STEPS / 2 - 1) * STATE_DIM + 2] == target[2], "c1a");

    // model dynamics constraint x = Ax + Bu
    for (uint e = 0; e < NUM_STEPS - 1; e++)
    {
        ostringstream v_name_cx, v_name_cy, v_name_cang;
        GRBLinExpr le, re;

        v_name_cx << "cx" << e + 2;
        le = state_vars[e * STATE_DIM] + control_vars[e * CONTROL_DIM];
        re = state_vars[(e + 1) * STATE_DIM];
        model->addConstr(le == re, v_name_cx.str());

        v_name_cy << "cy" << e + 2;
        le = state_vars[e * STATE_DIM + 1] + control_vars[e * CONTROL_DIM + 1];
        re = state_vars[(e + 1) * STATE_DIM + 1];
        model->addConstr(le == re, v_name_cy.str());

        v_name_cang << "ca" << e + 2;
        le = state_vars[e * STATE_DIM + 2] + control_vars[e * CONTROL_DIM + 2];
        re = state_vars[(e + 1) * STATE_DIM + 2];
        model->addConstr(le == re, v_name_cang.str());

    }

    // for (uint e = 0; e < NUM_STEPS-1; e++)
    // {
    //     ostringstream vnamecx, vnamecy, vnamecsin, vnameccos;
    //     GRBQuadExpr le, re;

    //     vnamecx << "cx" << e+2;
    //     le = state_vars[e*2] + control_vars[e*2]*sin_cos_vars[e*2+1];
    //     re = state_vars[(e+1)*2];
    //     model->addQConstr( le == re, vnamecx.str());

    //     vnamecy << "cy" << e+2;
    //     le = state_vars[e*2+1] + control_vars[e*2]*sin_cos_vars[e*2];
    //     re = state_vars[(e+1)*2+1];
    //     model->addQConstr(le == re, vnamecy.str());

    //     // vnamecsin << "csin" << e+2;
    //     // model->addGenConstrSin(control_vars[e*2+1], sin_cos_vars[e*2], vnamecsin.str());
    //     // vnameccos << "ccos" << e+2;
    //     // model->addGenConstrCos(control_vars[e*2+1], sin_cos_vars[e*2+1], vnameccos.str());
    // }

    // Quadratic constraints as the sum of the squared modules of all controls
    //obj = 0;  in .h
    for (uint e = 0; e < NUM_STEPS - 1; e++)
    {
        obj += control_vars[e * CONTROL_DIM] * control_vars[e * CONTROL_DIM];
        obj += control_vars[e * CONTROL_DIM + 1] * control_vars[e * CONTROL_DIM + 1];
        obj += control_vars[e * CONTROL_DIM + 2] * control_vars[e * CONTROL_DIM + 2];

        //obj += (state_vars[e*2]-state_vars[(e+1)*2])*(state_vars[e*2]-state_vars[(e+1)*2]);
        //obj += (state_vars[e*2+1]-state_vars[(e+1)*2+1])*(state_vars[e*2+1]-state_vars[(e+1)*2+1]);
    }
    model->update();
}

void SpecificWorker::optimize(const StateVector &current_state, const Obstacles &obstacles)
{
    try
    {
        model->remove(model->getConstrByName("c1x"));
        model->remove(model->getConstrByName("c1y"));
        model->remove(model->getConstrByName("c1a"));

        // remove all obstacle restrictions
        for (auto &constr : obs_contraints)
        {
            model->remove(constr.or_var);
            model->remove(constr.or_constraint);
            model->remove(constr.final_constraint);
            for(auto &p : constr.pdata)
            {
                model->remove(p.and_var);
                model->remove(p.and_constraint);
                for(auto &lv : p.line_vars)
                    model->remove(lv);
                p.line_vars.clear();
                for(auto &lc : p.line_constraints)
                    model->remove(lc);
                p.line_constraints.clear();
            }
            constr.pdata.clear();
        }
        obs_contraints.clear();

        model->update();
        model->addConstr(state_vars[(NUM_STEPS - 1) * STATE_DIM] == current_state.x(), "c1x");
        model->addConstr(state_vars[(NUM_STEPS - 1) * STATE_DIM + 1] == current_state.y(), "c1y");
        model->addConstr(state_vars[(NUM_STEPS / 2 - 1) * STATE_DIM + 2] == current_state[2], "c1a");

        // add new obstacle restrictions
        for (uint e = 0; e < NUM_STEPS; e++)
        {
            ObsData obs_data;
            for (auto &&[k, obs] : iter::enumerate(obstacles))
            {
                auto obs_line = std::get<Line>(obs);  // get Line form the tupla
                ObsData::PolyData pdata;
                // create line variables for the current polygon and make them equal to robot's distance to line
                for (auto &&[l, lines] : iter::enumerate(obs_line))
                {
                    auto line_var = model->addVar(0.0, 1.0, 0.0, GRB_BINARY);
                    auto &[A, B, C] = lines;
                    GRBGenConstr l_constr = model->addGenConstrIndicator(line_var, 1, A * state_vars[e * STATE_DIM] + B * state_vars[e * STATE_DIM + 1] + C,
                                                                         GRB_GREATER_EQUAL, 0);
                    pdata.line_vars.push_back(line_var);
                    pdata.line_constraints.push_back(l_constr);
                }
                // The polygon is finished. Create the AND variable for the polygon and AND constraint with all former live variables
                pdata.and_var = model->addVar(0.0, 1.0, 0.0, GRB_BINARY);
                GRBVar line_vars[pdata.line_vars.size()];   // extract line_vars to pass them to GenContrAnd as a C array
                for(auto &&[n, ac] : iter::enumerate(pdata.line_vars))
                    line_vars[n] = ac;
                pdata.and_constraint = model->addGenConstrAnd(pdata.and_var, line_vars, pdata.line_vars.size());
                obs_data.pdata.push_back(pdata);
            }
            // All polygons are finished. Now we create the OR variable and constraint with all AND variables
            obs_data.or_var = model->addVar(0.0, 1.0, 0.0, GRB_BINARY);
            GRBVar and_vars[obs_data.pdata.size()];
            for(auto &&[n, ac] : iter::enumerate(obs_data.pdata))
                and_vars[n] = ac.and_var;
            obs_data.or_constraint = model->addGenConstrOr(obs_data.or_var, and_vars, obs_data.pdata.size());
            obs_data.final_constraint = model->addConstr(obs_data.or_var, GRB_EQUAL,  1.0);  //
            obs_contraints.push_back(obs_data);
        }

        model->update();
        model->setObjective(obj, GRB_MINIMIZE);
        model->optimize();
    }
    catch (GRBException e)
    {
        std::cout << "Error code = " << e.getErrorCode() << std::endl;
        std::cout << e.getMessage() << std::endl;
        std::terminate();
    }
    catch(...)
    { std::cout << "Exception during optimization" << std::endl;   }

    // cout << "before optimizing" << endl;
    // for(uint e = 0; e < NUM_STEPS; e++)
    // {
    //     float x = state_vars[e*2].get(GRB_DoubleAttr_Start);
    //     cout << state_vars[e*2].get(GRB_StringAttr_VarName) << " " << x << endl;
    //     float y = state_vars[e*2+1].get(GRB_DoubleAttr_Start);
    //     cout << state_vars[e*2+1].get(GRB_StringAttr_VarName) << " " << y << endl;
    
    // }

    // cout << "after optimizing" << endl;
//    path.clear();
//    for(uint e = 0; e < NUM_STEPS; e++)
//    {
//        float x = state_vars[e * STATE_DIM].get(GRB_DoubleAttr_X);
//        // cout << state_vars[e*2].get(GRB_StringAttr_VarName) << " "
//        // << x << endl;
//        float y = state_vars[e * STATE_DIM + 1].get(GRB_DoubleAttr_X);
//        // cout << state_vars[e*2+1].get(GRB_StringAttr_VarName) << " "
//        // << y << endl;
//        QVec p =  innerModel->transform("world", QVec::vec3(x, 0., y), "base");
//        path.emplace_back(QPointF(p[0], p[2]));
//    }
    // for(uint e = 0; e < NUM_STEPS; e++)
    // {
    //     float x = control_vars[e*2].get(GRB_DoubleAttr_X);
    //     cout << control_vars[e*2].get(GRB_StringAttr_VarName) << " "
    //      << x << endl;
    //     float y = control_vars[e*2+1].get(GRB_DoubleAttr_X);
    //     cout << control_vars[e*2+1].get(GRB_StringAttr_VarName) << " "
    //      << y << endl;
    // }
}


//////////////////////////////////////////////////////////////////////////////////////////////////////

SpecificWorker::Obstacles SpecificWorker::compute_laser_partitions(QPolygonF  &laser_poly)  //robot coordinates
{
    TPPLPartition partition;
    TPPLPoly poly_part;
    TPPLPolyList parts;

    poly_part.Init(laser_poly.size());
    poly_part.SetHole(false);
    for(auto &&[i, l] : iter::enumerate(laser_poly))
    {
        poly_part[i].x = l.x();
        poly_part[i].y = l.y();
    }
    poly_part.SetOrientation(TPPL_CCW);
    //int r = partition.ConvexPartition_HM(&poly, &parts);
    int r = partition.ConvexPartition_OPT(&poly_part, &parts);
    qInfo() << __FUNCTION__ << "Ok: " << r << "Num vertices:" << poly_part.GetNumPoints() << "Num res polys: " << parts.size();

    Obstacles obstacles;
    for(auto &poly_res : parts)
    {
        //color.setRgb(qrand() % 255, qrand() % 255, qrand() % 255);
        auto num_points = poly_res.GetNumPoints();

        // generate QPolygons for drawing
        QPolygonF poly_draw(num_points);
        std::generate(poly_draw.begin(), poly_draw.end(), [poly_res, k=0, robot = robot_polygon]() mutable
        {
            auto &p = poly_res.GetPoint(k++);
            return robot->mapToScene(QPointF(p.x, p.y));  //convert to world coordinates
        });

        // generate vector of <A,B,C> tuples
        Line line(num_points);
        std::generate(line.begin(), line.end(),[poly_res, k=0, num_points]() mutable
        {
            float x1 = poly_res.GetPoint(k).x;
            float y1 = poly_res.GetPoint(k).y;
            float x2 = poly_res.GetPoint((++k) % num_points).x;
            float y2 = poly_res.GetPoint((k) % num_points).y;
            return std::make_tuple(y1 - y2, x2 - x1, -((y1 - y2)*x1 + (x2 - x1)*y1));
        });
        obstacles.emplace_back(std::make_tuple(line, poly_draw));
    }
    return obstacles;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////
void SpecificWorker::init_drawing( Grid<>::Dimensions dim)
{
    graphicsView->setScene(&scene);
    graphicsView->setMinimumSize(400,400);
    scene.setSceneRect(dim.HMIN, dim.VMIN, dim.WIDTH, dim.HEIGHT);
    graphicsView->scale(1, -1);
    graphicsView->fitInView(scene.sceneRect(), Qt::KeepAspectRatio );
    graphicsView->show();
    connect(&scene, &MyScene::new_target, this, [this](QGraphicsSceneMouseEvent *e)
    {
        qDebug() << "Lambda SLOT: " << e->scenePos();
        target_buffer.put(e->scenePos());
        atTarget = false;
    });

    //Draw
    custom_plot.setParent(signal_frame);
    custom_plot.xAxis->setLabel("time");
    custom_plot.yAxis->setLabel("vx-blue vy-red vw-green dist-magenta ew-black");
    custom_plot.xAxis->setRange(0, 200);
    custom_plot.yAxis->setRange(-1500, 1500);
    xGraph = custom_plot.addGraph();
    xGraph->setPen(QColor("blue"));
    yGraph = custom_plot.addGraph();
    yGraph->setPen(QColor("red"));
    wGraph = custom_plot.addGraph();
    wGraph->setPen(QColor("green"));
    exGraph = custom_plot.addGraph();
    exGraph->setPen(QColor("magenta"));
    ewGraph = custom_plot.addGraph();
    ewGraph->setPen(QColor("black"));
    custom_plot.resize(signal_frame->size());
    custom_plot.show();

    //robot
    QPolygonF poly2;
    float size = ROBOT_LENGTH / 2.f;
    poly2 << QPoint(-size, -size)
          << QPoint(-size, size)
          << QPoint(-size / 3, size * 1.6)
          << QPoint(size / 3, size * 1.6)
          << QPoint(size, size)
          << QPoint(size, -size);
    QColor rc("DarkRed"); rc.setAlpha(80);
    robot_polygon = scene.addPolygon(poly2, QPen(QColor("DarkRed")), QBrush(rc));
    robot_polygon->setZValue(5);
    try
    {
        RoboCompGenericBase::TBaseState bState;
        omnirobot_proxy->getBaseState(bState);
        robot_polygon->setRotation(qRadiansToDegrees(bState.alpha));
        robot_polygon->setPos(bState.x, bState.z);
    }
    catch(const Ice::Exception &e){};;

    connect(splitter, &QSplitter::splitterMoved, [this](int pos, int index)
        {  custom_plot.resize(signal_frame->size()); graphicsView->fitInView(scene.sceneRect(), Qt::KeepAspectRatio); });
}

RoboCompGenericBase::TBaseState SpecificWorker::read_base()
{
    RoboCompGenericBase::TBaseState bState;
    try
    {
        omnirobot_proxy->getBaseState(bState);
        innerModel->updateTransformValues("base", bState.x, 0, bState.z, 0, -bState.alpha, 0);
        robot_polygon->setRotation(qRadiansToDegrees(bState.alpha));
        robot_polygon->setPos(bState.x, bState.z);
    }
    catch(const Ice::Exception &e)
    { 
    //    std::cout << "Error reading from Camera" << e << std::endl;
    }
    return bState;
}

QPolygonF SpecificWorker::read_laser()
{
    QPolygonF laser_poly;
    try
    {
        auto ldata = laser_proxy->getLaserData();

        // simplify laser contour with Ramer-Douglas-Peucker
        std::vector<Point> plist(ldata.size());
        std::generate(plist.begin(), plist.end(), [ldata, k=0]() mutable
                    { auto &l = ldata[k++]; return std::make_pair(l.dist * sin(l.angle), l.dist * cos(l.angle));});
        vector<Point> pointListOut;
        ramer_douglas_peucker(plist, 50, pointListOut);
        laser_poly.resize(pointListOut.size());
//        std::generate(laser_poly.begin(), laser_poly.end(), [pointListOut, this, k=0]() mutable
//                    { auto &p = pointListOut[k++]; return robot_polygon->mapToScene(QPointF(p.first, p.second));});
        std::generate(laser_poly.begin(), laser_poly.end(), [pointListOut, this, k=0]() mutable
                      { auto &p = pointListOut[k++]; return QPointF(p.first, p.second);});
    }
    catch(const Ice::Exception &e)
    { std::cout << "Error reading from Laser" << e << std::endl;}
    return laser_poly;  // robot coordinates
}

void SpecificWorker::fill_grid(const QPolygonF &laser_poly)
{
    for(auto &[k, v] : grid)
        if(laser_poly.containsPoint(QPointF(k.x, k.z), Qt::OddEvenFill))
            v.free = true;
        else
            v.free = false;
    grid.draw(&scene);
}

void SpecificWorker::draw_laser(const QPolygonF &poly) // robot coordinates
{
    static QGraphicsItem *laser_polygon = nullptr;
    if (laser_polygon != nullptr)
        scene.removeItem(laser_polygon);

    QColor color("LightGreen");
    color.setAlpha(40);
    laser_polygon = scene.addPolygon(robot_polygon->mapToScene(poly), QPen(QColor("DarkGreen"), 30), QBrush(color));
    laser_polygon->setZValue(3);
}

void SpecificWorker::draw_path(const std::vector<QPointF> &path)
{
    static std::vector<QGraphicsEllipseItem *> path_paint;
    static QString path_color = "#FF00FF";

    for(auto p : path_paint)
        scene.removeItem(p);
    path_paint.clear();
    for(auto &p : path)
        path_paint.push_back(scene.addEllipse(p.x()-25, p.y()-25, 50 , 50, QPen(path_color), QBrush(QColor(path_color))));
}

void SpecificWorker::draw_target(const RoboCompGenericBase::TBaseState &bState, QPointF t)
{
    if (target_draw) scene.removeItem(target_draw);
    target_draw = scene.addEllipse(t.x() - 50, t.y() - 50, 100, 100, QPen(QColor("green")), QBrush(QColor("green")));
    // angular reference obtained from line joinning robot an target when  clicking
    float tr_x = t.x() - bState.x;
    float tr_y = t.y() - bState.z;
    float ref_ang = -atan2(tr_x, tr_y);   // signo menos para tener ángulos respecto a Y CCW
    auto ex = t.x() + 350 * sin(-ref_ang);
    auto ey = t.y() + 350 * cos(-ref_ang);  //OJO signos porque el ang está respecto a Y CCW
    auto line = scene.addLine(t.x(), t.y(), ex, ey, QPen(QBrush(QColor("green")), 20));
    line->setParentItem(target_draw);
    auto ball = scene.addEllipse(ex - 25, ey - 25, 50, 50, QPen(QColor("green")), QBrush(QColor("green")));
    ball->setParentItem(target_draw);
    cont = 0;
    xGraph->data()->clear();
    wGraph->data()->clear();
    yGraph->data()->clear();
    exGraph->data()->clear();
    ewGraph->data()->clear();
}

void SpecificWorker::stop_robot()
{
    omnirobot_proxy->setSpeedBase(0, 0, 0);
    std::cout << "FINISH" << std::endl;
    atTarget = true;
}

void SpecificWorker::draw(const ControlVector &control, float pos_error, float rot_error)
{
    std::vector<QPointF> path(NUM_STEPS);
    std::generate(path.begin(), path.end(), [i=innerModel, s=state_vars, d=STATE_DIM, e=0]() mutable
    {
        QVec p =  i->transform("world", QVec::vec3(s[e*d].get(GRB_DoubleAttr_X), 0., s[e*d+1].get(GRB_DoubleAttr_X)), "base");
        e++;
        return QPointF(p.x(), p.z());
    });
    draw_path(path);
    xGraph->addData(cont, control.x());
    yGraph->addData(cont, control.y());
    wGraph->addData(cont, control[2]*300);
    exGraph->addData(cont, pos_error);
    ewGraph->addData(cont,rot_error*300);
    cont++;
    custom_plot.replot();
}

void SpecificWorker::draw_partitions(const Obstacles &obstacles, bool print)
{
    static std::vector<QGraphicsPolygonItem *> polys_ptr{};
    for (auto p: polys_ptr)
        scene.removeItem(p);
    polys_ptr.clear();

    QColor color("LightBlue");
    for(auto &obs : obstacles)
    {
        bool inside = true;
        for (auto &[A, B, C] : std::get<Line>(obs))
            inside = inside and C>0;
        if(inside)
            polys_ptr.push_back(scene.addPolygon(std::get<QPolygonF>(obs), QPen(color, 30), QBrush(color)));
        else
            polys_ptr.push_back(scene.addPolygon(std::get<QPolygonF>(obs), QPen(color, 30)));
        // color.setRgb(qrand() % 255, qrand() % 255, qrand() % 255);
    }

    if(print)
    {
        qInfo() << "--------- LINES ------------";
        for(auto &[ll, _] : obstacles)
        {
            for (auto &[a, b, c] : ll)
                qInfo() << a << b << c;
            qInfo() << "-----------------------";
        }
    }
}

void SpecificWorker::ramer_douglas_peucker(const vector<Point> &pointList, double epsilon, vector<Point> &out)
{
    if(pointList.size()<2)
    {
        qWarning() << "Not enough points to simplify";
        return;
    }

    // Find the point with the maximum distance from line between start and end
    auto line = Eigen::ParametrizedLine<float, 2>::Through(Eigen::Vector2f(pointList.front().first,pointList.front().second),
                                                           Eigen::Vector2f(pointList.back().first,pointList.back().second));
    auto max = std::max_element(pointList.begin()+1, pointList.end(), [line](auto &a, auto &b)
                { return line.distance(Eigen::Vector2f(a.first, a.second)) < line.distance(Eigen::Vector2f(b.first, b.second));});
    float dmax =  line.distance(Eigen::Vector2f((*max).first, (*max).second));

    // If max distance is greater than epsilon, recursively simplify
    if(dmax > epsilon)
    {
        // Recursive call
        vector<Point> recResults1;
        vector<Point> recResults2;
        vector<Point> firstLine(pointList.begin(), max + 1);
        vector<Point> lastLine(max, pointList.end());

        ramer_douglas_peucker(firstLine, epsilon, recResults1);
        ramer_douglas_peucker(lastLine, epsilon, recResults2);

        // Build the result list
        out.assign(recResults1.begin(), recResults1.end() - 1);
        out.insert(out.end(), recResults2.begin(), recResults2.end());
        if (out.size() < 2)
        {
            qWarning() << "Problem assembling output";
            return;
        }
    }
    else
    {
        //Just return start and end points
        out.clear();
        out.push_back(pointList.front());
        out.push_back(pointList.back());
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////
int SpecificWorker::startup_check()
{
	std::cout << "Startup check" << std::endl;
	QTimer::singleShot(200, qApp, SLOT(quit()));
	return 0;
}

