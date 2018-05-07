#include <iostream>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/search/kdtree.h>

#include <ros/ros.h>
#include <ros/console.h>
#include <sensor_msgs/PointCloud2.h>
#include <geometry_msgs/Vector3.h>
#include <geometry_msgs/PoseStamped.h>
#include <nav_msgs/Odometry.h>
#include <Eigen/Eigen>
#include <math.h>
#include <random>

#define inf 99999999.0

using namespace std;

pcl::search::KdTree<pcl::PointXYZ> kdtreeLocalMap;
vector<int>     pointIdxRadiusSearch;
vector<float>   pointRadiusSquaredDistance;        

random_device rd;
default_random_engine eng(rd());
uniform_real_distribution<double>  rand_x;
uniform_real_distribution<double>  rand_y;
uniform_real_distribution<double>  rand_w;
uniform_real_distribution<double>  rand_h;

ros::Publisher _local_map_pub;
ros::Publisher _all_map_pub;
ros::Subscriber _odom_sub;

vector<double> _state;

int _ObsNum;
double _x_l, _x_h, _y_l, _y_h;
double _w_l, _w_h, _h_l, _h_h;
double _resolution;
double z_limit;
double _SenseRate;
double _z_limit;
double _sensing_range;

bool _map_ok = false;
bool _has_odom = false;

sensor_msgs::PointCloud2 localMap_pcd;
sensor_msgs::PointCloud2 globalMap_pcd;
sensor_msgs::PointCloud2 localMapInflate_pcd;
pcl::PointCloud<pcl::PointXYZ> cloudMap;

void RandomMapGenerate()
{    
      pcl::PointXYZ pt_random;

      rand_x = uniform_real_distribution<double>(_x_l, _x_h );
      rand_y = uniform_real_distribution<double>(_y_l, _y_h );
      rand_w = uniform_real_distribution<double>(_w_l, _w_h);
      rand_h = uniform_real_distribution<double>(_h_l, _h_h);

      for(int i = 0; i < _ObsNum; i ++)
      {
         double x, y, w, h; 
         x    = rand_x(eng);
         y    = rand_y(eng);
         w    = rand_w(eng);

         int widNum = ceil(w/_resolution);

         for(int r = -widNum / 2; r < widNum / 2; r ++ )
            for(int s = -widNum / 2; s < widNum / 2; s ++ ){
               h    = rand_h(eng);  
               int heiNum = ceil(h/_resolution);
               for(int t = 0; t < heiNum; t ++ ){
                  pt_random.x = x + r * _resolution;
                  pt_random.y = y + s * _resolution;
                  pt_random.z = t * _resolution;
                  cloudMap.points.push_back( pt_random );
               }
            }
      }

      cloudMap.width = cloudMap.points.size();
      cloudMap.height = 1;
      cloudMap.is_dense = true;

      ROS_WARN("Finished generate random map ");
      
      kdtreeLocalMap.setInputCloud( cloudMap.makeShared() ); 

      _map_ok = true;
}

void rcvOdometryCallbck(const nav_msgs::Odometry odom)
{
      if (odom.child_frame_id == "X" || odom.child_frame_id == "O") return ;
      _has_odom = true;

      _state = {
         odom.pose.pose.position.x, 
         odom.pose.pose.position.y, 
         odom.pose.pose.position.z, 
         odom.twist.twist.linear.x,
         odom.twist.twist.linear.y,
         odom.twist.twist.linear.z,
         0.0, 0.0, 0.0
      };
}

int i = 0;
void pubSensedPoints()
{     
      if(i < 100 )
      {
         pcl::toROSMsg(cloudMap, globalMap_pcd);
         globalMap_pcd.header.frame_id = "world";
         _all_map_pub.publish(globalMap_pcd);
      }

      i ++;
      if(!_map_ok || !_has_odom)
         return;

      pcl::PointCloud<pcl::PointXYZ> localMap;
      pcl::PointCloud<pcl::PointXYZ> localMapInflate;

      pcl::PointXYZ searchPoint(_state[0], _state[1], _state[2]);
      pointIdxRadiusSearch.clear();
      pointRadiusSquaredDistance.clear();
      
      pcl::PointXYZ ptInflation;
      pcl::PointXYZ ptInNoflation;

      if(isnan(searchPoint.x) || isnan(searchPoint.y) || isnan(searchPoint.z))
         return;

      if ( kdtreeLocalMap.radiusSearch (searchPoint, _sensing_range, pointIdxRadiusSearch, pointRadiusSquaredDistance) > 0 )
      {
         for (size_t i = 0; i < pointIdxRadiusSearch.size (); ++i){
            ptInNoflation = cloudMap.points[pointIdxRadiusSearch[i]];
            localMap.points.push_back(ptInNoflation);
         }

      }
      else{
         ROS_ERROR("[Map server] No obstacles .");
         return;
      }

      localMap.width = localMap.points.size();
      localMap.height = 1;
      localMap.is_dense = true;
      
      pcl::toROSMsg(localMap, localMap_pcd);
      localMap_pcd.header.frame_id = localMapInflate_pcd.header.frame_id = "world";         
      _local_map_pub.publish(localMap_pcd);         
}

int main (int argc, char** argv) {
        
      ros::init (argc, argv, "random_map_sensing");
      ros::NodeHandle n( "~" );

      _local_map_pub =
            n.advertise<sensor_msgs::PointCloud2>("RandomMap", 1);                      

      _all_map_pub =
            n.advertise<sensor_msgs::PointCloud2>("AllMap", 1);                      
      
      _odom_sub  = 
            n.subscribe( "odometry", 50, rcvOdometryCallbck );

      n.param("mapBoundary/lower_x", _x_l,       0.0);
      n.param("mapBoundary/upper_x", _x_h,     100.0);
      n.param("mapBoundary/lower_y", _y_l,       0.0);
      n.param("mapBoundary/upper_y", _y_h,     100.0);
      n.param("ObstacleShape/lower_rad", _w_l,   0.3);
      n.param("ObstacleShape/upper_rad", _w_h,   0.8);
      n.param("ObstacleShape/lower_hei", _h_l,   3.0);
      n.param("ObstacleShape/upper_hei", _h_h,   7.0);
      n.param("ObstacleShape/z_limit", _z_limit, 5.0);
      n.param("LocalBoundary/radius",  _sensing_range, 10.0);
      
      n.param("ObstacleNum", _ObsNum,  30);
      n.param("Resolution",  _resolution, 0.2);
      n.param("SensingRate", _SenseRate, 10.0);

      RandomMapGenerate();

      ros::Rate loop_rate(_SenseRate);
      
      while (ros::ok())
      {
        pubSensedPoints();
        ros::spinOnce();
        loop_rate.sleep();
      }
}