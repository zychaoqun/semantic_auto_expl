/**
 * OpenDoorRecognitionApp.cpp
 * Door recognition module. Is subscribed to the kinect point cloud topic "kinect/depth/points"
 * which is used to recognize open doors using the algorithm of X (). The currently recognized
 * open door is then published via "door/open_door".
 *
 * @author Matej Stanic (m.stanic@student.uibk.ac.at)
 * @version 1.0
 * @date 03.06.2017
 */

#include "semantic_mapping/OpenDoorRecognitionApp.h"

using namespace pcl;

/**
 * @brief OpenDoorRecognitionApp::OpenDoorRecognitionApp
 * Constructor. Registers subscriber for depth points and publisher for recognized
 * open doors.
 */
OpenDoorRecognitionApp::OpenDoorRecognitionApp(){
    input_cloud_sub = n.subscribe("kinect/depth/points", 1, &OpenDoorRecognitionApp::cloudCallback, this);
    map_pub = n.advertise<semantic_mapping_msgs::DoorMessage>("door/open_door",1000);
    gettimeofday(&tp, NULL);
    console::setVerbosityLevel(console::L_ALWAYS);
    ROS_INFO("Open door recognition node started.");
}

/**
 * @brief OpenDoorRecognitionApp::~OpenDoorRecognitionApp
 * Destructor.
 */
OpenDoorRecognitionApp::~OpenDoorRecognitionApp(){
    ROS_INFO("Open door recognition node terminated.");
}

bool OpenDoorRecognitionApp::getLastCorrect(){
    return last_correct;
}

void OpenDoorRecognitionApp::setDebugFlag(){
    debug = true;
}

void OpenDoorRecognitionApp::setRansacEpsAngle(double d){
    ransac_eps_angle = d;
}

void OpenDoorRecognitionApp::setRansacDistanceThreshold(double d){
    ransac_distance_threshold = d;
}

void OpenDoorRecognitionApp::setRansacMaxIterations(double d){
    ransac_max_iterations = d;
}

void OpenDoorRecognitionApp::setAlpha(double d){
    alpha = d;
}

void OpenDoorRecognitionApp::setDoorWidthMin(double d){
    door_width_min = d;
}

void OpenDoorRecognitionApp::setDoorWidthMax(double d){
    door_width_max = d;
}

void OpenDoorRecognitionApp::setDoorHeight(double d){
    door_height = d;
}

void OpenDoorRecognitionApp::setKinectHeight(double d){
    kinect_height = d;
}

void OpenDoorRecognitionApp::setStripeHeight(double d){
    stripe_height = d;
}

void OpenDoorRecognitionApp::setStripeNoise(double d){
    stripe_noise = d;
}

void OpenDoorRecognitionApp::setCubeDistance(double d){
    cube_distance = d;
}

void OpenDoorRecognitionApp::setMaxNoise(int i){
    max_noise = i;
}

/**
 * @brief comparePoints
 * Comparator function for the x value of two points.
 */
bool comparePoints(const PointXYZ& p1, const PointXYZ& p2) {
    return (p1.x<p2.x);
}

/**
 * @brief OpenDoorRecognitionApp::euclideanDistance
 * Calculates the Euclidean distance between two points.
 */
double OpenDoorRecognitionApp::euclideanDistance(const PointXYZ& p1, const PointXYZ& p2) {
    return (sqrt(pow(p1.x-p2.x,2)+pow(p1.y-p2.y,2)+pow(p1.z-p2.z,2)));
}

/**
 * @brief OpenDoorRecognitionApp::simpleVis
 * @param cloud
 * @param floor_cloud
 * @param wall_cloud
 * @return
 */
boost::shared_ptr<pcl::visualization::PCLVisualizer> OpenDoorRecognitionApp::simpleVis (PointCloud<PointXYZ>::ConstPtr cloud1, PointCloud<PointXYZ>::ConstPtr cloud2, PointCloud<PointXYZ>::ConstPtr cloud3, PointCloud<PointXYZ>::ConstPtr cloud4){
  boost::shared_ptr<pcl::visualization::PCLVisualizer> viewer (new pcl::visualization::PCLVisualizer ("3D Viewer"));
  viewer->setBackgroundColor (0, 0, 0);
  viewer->addPointCloud<PointXYZ> (cloud1, "cloud 1");
  viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_COLOR, 1.0,0,0, "cloud 1");
  viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 1, "cloud 1");
  viewer->addPointCloud<PointXYZ> (cloud2, "cloud 2");
  viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_COLOR, 0,1.0,0, "cloud 2");
  viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 1, "cloud 2");
  viewer->addPointCloud<PointXYZ> (cloud3, "cloud 3");
  viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_COLOR, 0,0,1.0, "cloud 3");
  viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 1, "cloud 3");
  viewer->addPointCloud<PointXYZ> (cloud4, "cloud 4");
  viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_COLOR, 1.0,0,1.0, "cloud 4");
  viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 1, "cloud 4");
  viewer->addCoordinateSystem (1.0);
  viewer->initCameraParameters ();
  return (viewer);
}

/**
 * @brief OpenDoorRecognitionApp::cloudCallback
 * Callback for incoming kinect point clouds. Every time a point cloud is received the open
 * door recognition algorithm by X () is executed. Also publishes every open door found.
 */
void OpenDoorRecognitionApp::cloudCallback(const sensor_msgs::PointCloud2ConstPtr& msg){
    finished = false;
    // Obtain input point cloud from topic message
    ros::Time cloudtime = ros::Time::now();
    PointCloud<PointXYZ>::Ptr input_cloud(new PointCloud<PointXYZ>), filtered_plane (new PointCloud<PointXYZ>);
    fromROSMsg(*msg, *input_cloud);

    // Nullpointer check
    if(input_cloud == nullptr){
        ROS_WARN("input cloud is a nullptr.");
        finished = true;
        return;
    }
    int i = 0, nr_points = (int) input_cloud->points.size();
    ROS_DEBUG("Received cloud with %d points", nr_points);

    // Repeat open door recognition algorithm while percentage of unobserved cloud points > alpha
    while(input_cloud->points.size () > alpha * nr_points && i < 1){
        std::clock_t begin = clock();

        // Use RANSAC to find the dominant wall
        // Dominant wall has to be parallel to y-Axis (kinect tilt angle = 0!)
        ModelCoefficients::Ptr coefficients(new ModelCoefficients ());
        PointIndices::Ptr inliers(new PointIndices ());
        ExtractIndices<PointXYZ> extract;
        SACSegmentation<PointXYZ> seg;
        seg.setOptimizeCoefficients(true);
        seg.setModelType(SACMODEL_PARALLEL_PLANE);
        seg.setMethodType(SAC_RANSAC);
        Eigen::Vector3f y(0,1,0);
        seg.setAxis(y);
        seg.setEpsAngle(ransac_eps_angle);
        seg.setDistanceThreshold (ransac_distance_threshold);
        seg.setMaxIterations(ransac_max_iterations);
        seg.setInputCloud (input_cloud);
        seg.segment (*inliers, *coefficients);

        // If no dominant wall is found algorithm has finished
        if (inliers->indices.size () == 0){
          ROS_WARN("Could not estimate a planar model for the given dataset.");
          finished = true;
          return;
        }

        // Extract the dominant wall plane
        extract.setInputCloud (input_cloud);
        extract.setIndices (inliers);
        extract.setNegative (false);
        extract.filter (*filtered_plane);

        // Extract stripe from plane which has a constant y-value >stripe_height<

        PointCloud<PointXYZ>::Ptr stripe_cloud (new PointCloud<PointXYZ>);
        for(PointXYZ point : *filtered_plane){
            if(point.y > stripe_height-stripe_noise && point.y < stripe_height+stripe_noise){
                stripe_cloud->points.push_back(point);
            }
        }

        // Check if valid stripe is obtained
        if (stripe_cloud->points.size () > 0){

            // Sort stripe by x-values
            ROS_DEBUG("stripe with %d points found.", (int) stripe_cloud->points.size());
            sort(stripe_cloud->points.begin(), stripe_cloud->points.end(), comparePoints);


            // new algorithm - simplified
            PointXYZ gap_start;
            PointXYZ gap_end;
            bool first_iter = true;
            for (PointXYZ point: stripe_cloud->points){
                gap_end = point;
                /*if (!first_iter){
                    ROS_DEBUG("Gap with width %f found.", euclideanDistance(gap_start,gap_end));
                }*/
                if (!first_iter && euclideanDistance(gap_start,gap_end) > door_width_min &&
                        euclideanDistance(gap_start,gap_end) < door_width_max){
                    ROS_DEBUG("Gap with width %f found.", euclideanDistance(gap_start,gap_end));
                    if(euclideanDistance(gap_start,gap_end) > door_width_min &&
                                euclideanDistance(gap_start,gap_end) < door_width_max){ // line 17

                        // Make a cube using the gap points
                        double x_min = std::min(gap_start.x, gap_end.x) + cube_distance;
                        double x_max = std::max(gap_start.x, gap_end.x) - cube_distance;
                        double y_min = std::min(-kinect_height+door_height, -kinect_height) + cube_distance;
                        double y_max = std::max(-kinect_height+door_height, -kinect_height) - cube_distance;
                        double z_min = std::min(gap_start.z, gap_end.z);
                        double z_max = std::max(gap_start.z, gap_end.z);

                        Eigen::Vector4f minPoint;
                        minPoint[0] = x_min;
                        minPoint[1] = y_min;
                        minPoint[2] = z_min;
                        Eigen::Vector4f maxPoint;
                        maxPoint[0] = x_max;
                        maxPoint[1] = y_max;
                        maxPoint[2] = z_max;

                        // Filter points that are in the cube
                        PointCloud<PointXYZ>::Ptr door_cloud (new PointCloud<PointXYZ>);
                        CropBox<PointXYZ> door_filter;
                        door_filter.setInputCloud(input_cloud);
                        door_filter.setMin(minPoint);
                        door_filter.setMax(maxPoint);
                        door_filter.filter(*door_cloud);

                        // DEBUG: Visualize point clouds
                        if (debug){
                            viewer = simpleVis(input_cloud, filtered_plane, stripe_cloud, door_cloud);
                            //viewer->addPlane(*coefficients, "plane");
                            while (!viewer->wasStopped()){
                                viewer->spinOnce(100);
                                boost::this_thread::sleep (boost::posix_time::microseconds (100000));
                            }
                        }

                        // Final check: if no points in cube then door detected! -> problem: kinect sensor is very noisy!
                        if(door_cloud->points.size() <= max_noise){
                            //double door_width = euclideanDistance(gap_start,gap_end);
                            //double door_pos_x = (gap_start.x+gap_end.x)/2;
                            //double door_pos_y = (gap_start.y+gap_end.y)/2;
                            //double door_pos_z = (gap_start.z+gap_end.z)/2;
                            //PointXYZ door_position(door_pos_x, door_pos_y, door_pos_z);
                            //ROS_INFO("Door %d (width = %f) detected with coordinates: [%f, %f, %f]", door_id_counter, door_width, door_pos_x, door_pos_y, door_pos_z);

                            // Publish open door to "door/open_door" topic
                            semantic_mapping_msgs::DoorMessage msg;
                            msg.header.seq = door_id_counter;
                            msg.header.stamp = cloudtime;
                            msg.pt_start.header.frame_id = "kinect_depth_optical_frame";
                            msg.pt_start.header.stamp = cloudtime;
                            msg.pt_start.point.x = gap_start.x;
                            msg.pt_start.point.y = gap_start.y;
                            msg.pt_start.point.z = gap_start.z;
                            msg.pt_end.header.frame_id = "kinect_depth_optical_frame";
                            msg.pt_end.header.stamp = cloudtime;
                            msg.pt_end.point.x = gap_end.x;
                            msg.pt_end.point.y = gap_end.y;
                            msg.pt_end.point.z = gap_end.z;
                            msg.quality = door_cloud->points.size();
                            msg.distance = (gap_start.z + gap_end.z) / 2;
                            msg.state = "open";
                            map_pub.publish(msg);
                            door_id_counter++;
                            ROS_INFO("%d points in door cube, found in %d iteration", (int)door_cloud->points.size(), i);
                            finished = true;
                            last_correct = true;
                            return;
                        } else {
                            ROS_DEBUG("Door found, but %d points in cube.", (int)door_cloud->points.size());
                        }
                    }
                }

                // Move further on the stripe and repeat
                gap_start = gap_end;
                first_iter = false;
            }

            std::clock_t end = clock();
            //double elapsed_ms = (double(end - begin) / CLOCKS_PER_SEC)*1000;
            ROS_DEBUG("No open door found.");
        } else {
            ROS_DEBUG("No stripe obtained.");
            finished = true;
        }
        // Remove the planar inliers, extract the rest
        extract.setNegative (true);
        extract.filter(*filtered_plane);
        *input_cloud = *filtered_plane;
        i++;
    }
    finished = true;
    last_correct = false;
    return;
}

/**
 * @brief OpenDoorRecognitionApp::spin
 * ROS node controller.
 */
void OpenDoorRecognitionApp::spin(){
    ros::Rate lr(10);
    while(ros::ok()){
      try {
        if(finished){
            ros::spinOnce();
        } else {
            ROS_INFO("Open door recognition node not finished.");
        }
        lr.sleep();
      } catch(std::runtime_error& err){
        ROS_ERROR("%s: %s", node_name.c_str(), err.what());
        std::exit(1);
      }
    }
    ROS_ERROR("Ros not ok, open_door_detector finished.");
}

