#!/usr/bin/python3
# -*- coding: utf-8 -*-
#
#    Copyright (C) 2022 by YOUR NAME HERE
#
#    This file is part of RoboComp
#
#    RoboComp is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    RoboComp is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with RoboComp.  If not, see <http://www.gnu.org/licenses/>.
#

from PySide2.QtCore import QTimer
from PySide2.QtWidgets import QApplication
from rich.console import Console
from genericworker import *
import interfaces as ifaces
import numpy as np
import casadi as ca
import sys, signal
from loguru import logger
from pylab import plot, step, figure, legend, show, spy

sys.path.append('/opt/robocomp/lib')
console = Console(highlight=False)

class SpecificWorker(GenericWorker):
    def __init__(self, proxy_map, startup_check=False):
        super(SpecificWorker, self).__init__(proxy_map)
        self.Period = 1000
        if startup_check:
            self.startup_check()
        else:

            self.target_pose = [1, 1]  # meter
            self.initialize()

            self.timer.timeout.connect(self.compute)
            self.timer.setSingleShot(True)
            self.timer.start(self.Period)


    def __del__(self):
        """Destructor"""

    def setParams(self, params):
        # try:
        #	self.innermodel = InnerModel(params["InnerModelPath"])
        # except:
        #	traceback.print_exc()
        #	print("Error reading config params")
        return True


    @QtCore.Slot()
    def compute(self):
        try:
            currentPose = self.omnirobot_proxy.getBaseState()
            #print(currentPose)
        except:
            print("Error connecting to base")

        if abs(currentPose.z/1000 - self.target_pose[1]) > 0.01:

            # ---- initial values for solver ---
            self.opti.set_initial(self.pos_x, currentPose.x/1000)
            self.opti.set_initial(self.pos_y, currentPose.z/1000)
            self.opti.set_initial(self.phi, currentPose.alpha)

            # ---- solve NLP              ------
            p_opts = {"expand": True}
            s_opts = {"max_iter": 1000,  'print_level': 0}
            self.opti.solver("ipopt", p_opts, s_opts)  # set numerical backend
            sol = self.opti.solve()  # actual solve
            print(sol.value(self.v_x[0]*1000), sol.value(self.v_y[0]*1000), sol.value(self.v_rot[0]))

            try:
                pass
                #self.omnirobot_proxy.setSpeedBase(self.v_x[0], self.v_y[0], self.rot[0])
            except:
                print("Error connecting to base")

        #plot(sol.value(self.v_x), label="vx")
        #plot(sol.value(self.v_y), label="vy")
        #plot(sol.value(self.v_rot), label="vrot")
        print(sol.value(self.pos_y))
        plot(sol.value(self.pos_x), label="px")
        plot(sol.value(self.pos_y), label="py")
        legend(loc="upper left")
        show()

    #@logger.catch
    def initialize(self):

        self.N = 50  # number of control intervals
        self.opti = ca.Opti()  # Optimization problem

        # ---- state variables ---------
        self.X = self.opti.variable(3, self.N + 1)  # state trajectory
        self.pos_x = self.X[0, :]
        self.pos_y = self.X[1, :]
        self.phi = self.X[2, :]

        # ---- inputs variables ---------
        self.U = self.opti.variable(3, self.N)  # control trajectory (throttle)
        self.v_x = self.U[0, :]
        self.v_y = self.U[1, :]
        self.v_rot = self.U[2, :]

        self.T = self.opti.variable()  # final time
        self.target = self.opti.variable(2)

        # ---- cost function          ---------
        self.opti.set_initial(self.target, self.target_pose)
        self.opti.minimize(ca.sumsqr(self.v_x) +
                           ca.sumsqr(self.v_y) +
                           ca.sumsqr(self.v_rot) +
                           ca.sumsqr(self.X[0:2, -1]-self.target_pose))  # minimum length

        # ---- dynamic constraints --------
        # dx/dt = f(x, u)
        f = lambda x, u: ca.vertcat(ca.horzcat(ca.cos(x[2]), -ca.sin(x[2]), 0),
                                    ca.horzcat(ca.sin(x[2]), ca.cos(x[2]), 0),
                                    ca.horzcat(0, 0, 1)) @ u

        dt = self.T / self.N  # length of a control interval
        for k in range(self.N):  # loop over control intervals
            # Runge-Kutta 4 integration
            k1 = f(self.X[:, k], self.U[:, k])
            k2 = f(self.X[:, k] + dt / 2 * k1, self.U[:, k])
            k3 = f(self.X[:, k] + dt / 2 * k2, self.U[:, k])
            k4 = f(self.X[:, k] + dt * k3, self.U[:, k])
            x_next = self.X[:, k] + dt / 6 * (k1 + 2 * k2 + 2 * k3 + k4)
            self.opti.subject_to(self.X[:, k + 1] == x_next)  # close the gaps

        # ---- control constraints -----------
        self.opti.subject_to(self.opti.bounded(-0.3, self.v_x, 0.3))  # control is limited meters
        self.opti.subject_to(self.opti.bounded(-0.3, self.v_y, 0.3))  # control is limited
        self.opti.subject_to(self.opti.bounded(-1.5, self.v_rot, 1.5))  # control is limited

        # ---- misc. constraints  ----------
        #self.opti.subject_to(self.T >= 0)  # Time must be positive

        # ---- initial values  ----------
        try:
            currentPose = self.omnirobot_proxy.getBaseState()
        except:
            print("Error connecting to base")
            sys.exit(0)

        # initialize steps
        start = np.array([currentPose.x/1000, currentPose.z/1000])
        end = np.array(self.target_pose)
        step = (np.linalg.norm(end-start) / self.N )
        for i, L in enumerate(np.arange(0, 1, step)):
            r = start*(1-L) + end*L
            self.opti.set_initial(self.X[0, i], r[0])
            self.opti.set_initial(self.X[1, i], r[1])


    ######################################################################################################
    def startup_check(self):
        print(f"Testing RoboCompLaser.LaserConfData from ifaces.RoboCompLaser")
        test = ifaces.RoboCompLaser.LaserConfData()
        print(f"Testing RoboCompLaser.TData from ifaces.RoboCompLaser")
        test = ifaces.RoboCompLaser.TData()
        print(f"Testing RoboCompOmniRobot.TMechParams from ifaces.RoboCompOmniRobot")
        test = ifaces.RoboCompOmniRobot.TMechParams()
        QTimer.singleShot(200, QApplication.instance().quit)



