// Copyright (c) <2016>, <Nanyang Technological University> All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:

// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.

// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.

// 3. Neither the name of the copyright holder nor the names of its contributors
// may be used to endorse or promote products derived from this software without
// specific prior written permission.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "localization.h"

using namespace Eigen;


Localization::Localization( int N )
{
    solver = new Solver();

    solver->setBlockOrdering(false);

    se3blockSolver = new SE3BlockSolver(solver);

    optimizationsolver = new g2o::OptimizationAlgorithmLevenberg(se3blockSolver);

    optimizer.setAlgorithm(optimizationsolver);

    optimizer.setVerbose(true);

    iteration_max = 100;

    number_of_nodes = N;

    T=2/100;



}


void Localization::solve()
{
    optimizer.initializeOptimization();

    optimizer.optimize(iteration_max);
}




void Localization::addPoseEdge(const geometry_msgs::PoseWithCovarianceStamped::ConstPtr& pose_cov_)


{
    geometry_msgs::PoseWithCovarianceStamped pose_cov(*pose_cov_);  

    g2o::EdgeSE3 *edge = new g2o::EdgeSE3();

    edge->vertices()[0] = optimizer.vertex(0);

    edge->vertices()[1] = optimizer.vertex(1);

    Eigen::Isometry3d measurement;

    tf::poseMsgToEigen(pose_cov.pose.pose, measurement);
    // change to Isometry3d

    edge->setMeasurement(measurement);

    Eigen::Map<Eigen::MatrixXd> covariance(pose_cov.pose.covariance.data(), 6, 6);

    edge->setInformation(covariance.inverse());

    edge->setRobustKernel(new g2o::RobustKernelHuber());

    optimizer.addEdge(edge);

    edges_pose.push_back(edge);

    ROS_INFO("added pose edge id: %d", pose_cov.header.seq);
}




// add IMU pose edge

void Localization::addImuPoseEdge(const sensor_msgs::Imu::ConstPtr& Imu_pose_)

{  
    
     sensor_msgs::Imu Imu_pose(*Imu_pose_);


    //have the problem of synchronization of uwb range message and imu message. 

    // how to get the Vertex0_id from uwb->header.seq

    int measCount = uwb->header.seq;


    // vertices for the new measurment

    int vertex0_id = number_of_nodes*measCount+uwb->requester_idx; 

    // int vertex0_id = 10;




   
    Quaterniond direction = Quaterniond(Imu_pose.orientation.w,Imu_pose.orientation.x,Imu_pose.orientation.y,Imu_pose.orientation.z);

    Vector3d acceleration = Vector3d(Imu_pose.linear_acceleration.x, Imu_pose.linear_acceleration.y, Imu_pose.linear_acceleration.z);

    


    // for no fixed node and all are dynamic nodes: using approximation p(t)-p(t-1)~a(t)*T^2;
       




    // for four fixed node and one dynamic node  p(t)-p(t-1)=a(t)*T^2+p(t-1)-p(t-2);

    g2o::VertexSE3 *v1 = dynamic_cast<g2o::VertexSE3 *> (optimizer.vertex(vertex0_id-number_of_nodes));

    g2o::VertexSE3 *v2 = dynamic_cast<g2o::VertexSE3 *> (optimizer.vertex(vertex0_id-2*number_of_nodes));

    Vector3d pt1 = v1->estimate().translation();
    
    Vector3d pt2 = v2->estimate().translation();


    // d=p(t)-p(t-1)=a(t)*T^2+p(t-1)-p(t-2);

    Vector3d d = acceleration*pow(T,2)+pt1-pt2;


    // rotation calculation

     Eigen::Isometry3d pose;

     pose.translate(g2o::Vector3D(0, 0, 0) );

     pose.rotate(direction);
     
     Isometry3d delta= v1->estimate().inverse()*pose;


     // translation replace

     Vector3d replace = pose.rotation().inverse()*d;

     delta.matrix()(0,3) = replace(0);

     delta.matrix()(1,3) = replace(1);

     delta.matrix()(2,3) = replace(2);


     // add edge
    
     g2o::EdgeSE3 *edge = new g2o::EdgeSE3();
    
     edge->vertices()[0] = optimizer.vertex(vertex0_id);

     edge->vertices()[1] = optimizer.vertex(vertex0_id-number_of_nodes); 

     edge->setMeasurement(delta);

     Eigen::MatrixXd information = Eigen::MatrixXd::Identity(6, 6) * 0.01;

     information(3,3) = 0.0001;
     information(4,4) = 0.0001;
     information(5,5) = 0.0001;

     edge->setInformation(information.inverse());
    
     optimizer.addEdge( edge );

     edges_pose.push_back(edge); 


    
}





//subscribe range msg from uwb_as node

void Localization::addRangeEdge(const uwb_driver::UwbRange::ConstPtr& uwb)
{
    //current index for the measurement
    int measCount = uwb->header.seq;


    //add two vertices for the new measurment
    int vertex0_id = number_of_nodes*measCount+uwb->requester_idx;

    int vertex1_id = number_of_nodes*measCount+uwb->responder_idx;

    g2o::VertexSE3* v0 = new g2o::VertexSE3();

    v0->setId(vertex0_id);



    //previous vertex estimate  

    // but where to add the initial vertex pose

    g2o::VertexSE3* v0_p = dynamic_cast<g2o::VertexSE3*>(optimizer.vertex(vertex0_id-number_of_nodes));

    v0->setEstimate(v0_p->estimate());

    optimizer.addVertex(v0);

    poses.push_back(v0);



    g2o::VertexSE3* v1 = new g2o::VertexSE3();

    v1->setId(vertex1_id);

    v1->setEstimate((dynamic_cast<g2o::VertexSE3*>(optimizer.vertex(vertex1_id-number_of_nodes)))->estimate());

    optimizer.addVertex(v1);

    poses.push_back(v1);



    //add edge

    g2o::EdgeSE3Range *edge = new g2o::EdgeSE3Range();

    edge->vertices()[0] = optimizer.vertex(vertex0_id);

    edge->vertices()[1] = optimizer.vertex(vertex1_id);

    edge->setMeasurement(uwb->distance);

    Eigen::MatrixXd information = Eigen::MatrixXd::Zero(1, 1);

    information(0,0) = 0.0025;

    edge->setInformation(information.inverse());

    edge->setRobustKernel(new g2o::RobustKernelHuber());

    optimizer.addEdge(edge);

    edges_range.push_back(edge);



}





