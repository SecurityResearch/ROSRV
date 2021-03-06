
#include "rv/master.h"
#include "rv/xmlrpc_manager.h"
#include "ros/this_node.h"
#include "ros/init.h"
#include "ros/network.h"

#include <ros/console.h>
#include <ros/assert.h>

#include "rv/XmlRpc.h"

namespace rv
{

namespace master
{

uint32_t g_port = 0;
std::string g_host;
std::string g_uri;
ros::WallDuration g_retry_timeout;

void init(const ros::M_string& remappings)
{
  ros::M_string::const_iterator it = remappings.find("__master");
  if (it != remappings.end())
  {
    g_uri = it->second;
  }

  if (g_uri.empty())
  {
    char *master_uri_env = NULL;
    #ifdef _MSC_VER
      _dupenv_s(&master_uri_env, NULL, "REAL_MASTER_URI");
    #else
      master_uri_env = getenv("REAL_MASTER_URI");
    #endif
    if (!master_uri_env)
    {
      ROS_FATAL( "REAL_MASTER_URI is not defined in the environment. Either " \
                 "type the following or (preferrably) add this to your " \
                 "~/.bashrc file in order set up your " \
                 "local machine as a ROS master:\n\n" \
                 "export REAL_MASTER_URI=http://localhost:12345\n\n" \
                 "then, type 'roscore' in another shell to actually launch " \
                 "the master program.");
      ROS_BREAK();
    }

    g_uri = master_uri_env;

#ifdef _MSC_VER
    // http://msdn.microsoft.com/en-us/library/ms175774(v=vs.80).aspx
    free(master_uri_env);
#endif
  }

  // Split URI into
  if (!ros::network::splitURI(g_uri, g_host, g_port))
  {
    ROS_FATAL( "Couldn't parse the master URI [%s] into a host:port pair.", g_uri.c_str());
    ROS_BREAK();
  }
}

const std::string& getHost()
{
  return g_host;
}

uint32_t getPort()
{
  return g_port;
}

const std::string& getURI()
{
  return g_uri;
}

void setRetryTimeout(ros::WallDuration timeout)
{
  if (timeout < ros::WallDuration(0))
  {
    ROS_FATAL("retry timeout must not be negative.");
    ROS_BREAK();
  }
  g_retry_timeout = timeout;
}

bool check()
{
  XmlRpc::XmlRpcValue args, result, payload;
  args[0] = ros::this_node::getName();
  return execute("getPid", args, result, payload, false);
}

bool getTopics(V_TopicInfo& topics)
{
  XmlRpc::XmlRpcValue args, result, payload;
  args[0] = ros::this_node::getName();
  args[1] = ""; //TODO: Fix this

  if (!execute("getPublishedTopics", args, result, payload, true))
  {
    return false;
  }

  topics.clear();
  for (int i = 0; i < payload.size(); i++)
  {
    topics.push_back(TopicInfo(std::string(payload[i][0]), std::string(payload[i][1])));
  }

  return true;
}

bool getNodes(ros::V_string& nodes)
{
  XmlRpc::XmlRpcValue args, result, payload;
  args[0] = ros::this_node::getName();

  if (!execute("getSystemState", args, result, payload, true))
  {
    return false;
  }

  ros::S_string node_set;
  for (int i = 0; i < payload.size(); ++i)
  {
    for (int j = 0; j < payload[i].size(); ++j)
    {
      XmlRpc::XmlRpcValue val = payload[i][j][1];
      for (int k = 0; k < val.size(); ++k)
      {
        std::string name = payload[i][j][1][k];
        node_set.insert(name);
      }
    }
  }

  nodes.insert(nodes.end(), node_set.begin(), node_set.end());

  return true;
}

#if defined(__APPLE__)
boost::mutex g_xmlrpc_call_mutex;
#endif

bool execute(const std::string& method, const XmlRpc::XmlRpcValue& request, XmlRpc::XmlRpcValue& response, XmlRpc::XmlRpcValue& payload, bool wait_for_master)
{

  ros::WallTime start_time = ros::WallTime::now();

  std::string master_host = getHost();
  uint32_t master_port = getPort();
  XmlRpcClient *c = XMLRPCManager::instance()->getXMLRPCClient(master_host, master_port, "/");
  bool printed = false;
  bool slept = false;
  bool ok = true;


  do
  {
    bool b = false;
    {
#if defined(__APPLE__)
      boost::mutex::scoped_lock lock(g_xmlrpc_call_mutex);
#endif

      b = c->execute(method.c_str(), request, response);
    }



    ok = !ros::isShuttingDown() && !XMLRPCManager::instance()->isShuttingDown();

    if (!b && ok)
    {
      if (!printed && wait_for_master)
      {
        ROS_ERROR("[%s] Failed to contact master at [%s:%d].  %s", method.c_str(), master_host.c_str(), master_port, wait_for_master ? "Retrying..." : "");
        printed = true;
      }

      if (!wait_for_master)
      {
        XMLRPCManager::instance()->releaseXMLRPCClient(c);
        return false;
      }

      if (!g_retry_timeout.isZero() && (ros::WallTime::now() - start_time) >= g_retry_timeout)
      {
        ROS_ERROR("[%s] Timed out trying to connect to the master after [%f] seconds", method.c_str(), g_retry_timeout.toSec());
        XMLRPCManager::instance()->releaseXMLRPCClient(c);
        return false;
      }

      ros::WallDuration(0.05).sleep();
      slept = true;
    }
    else
    {
      if (!XMLRPCManager::instance()->validateXmlrpcResponse(method, response, payload))
      {
        XMLRPCManager::instance()->releaseXMLRPCClient(c);

        return false;
      }

      break;
    }

    ok = !ros::isShuttingDown() && !XMLRPCManager::instance()->isShuttingDown();
  } while(ok);

  if (ok && slept)
  {
    ROS_INFO("Connected to master at [%s:%d]", master_host.c_str(), master_port);
  }

  XMLRPCManager::instance()->releaseXMLRPCClient(c);

  
return true;
}

} // namespace master

} // namespace rv

