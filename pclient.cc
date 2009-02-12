#include "Poco/Net/StreamSocket.h"
#include "Poco/Net/SocketStream.h"
#include "Poco/Net/SocketAddress.h"
#include "Poco/Path.h"
#include "Poco/Exception.h"
#include "Poco/Thread.h"
#include <OpenMesh/Core/Utils/Endian.hh>
#include <iostream>
#include "ppmesh.hh"
#include "bitstring.hh"
#include "vertexid.hh"
#include "prender.hh"
#include "preceiver.hh"
#include "pvisiblepq.hh"
#include "logger.hh"

using Poco::Net::StreamSocket;
using Poco::Net::SocketStream;
using Poco::Net::SocketAddress;
using Poco::Path;
using Poco::Exception;
using Poco::Thread;


int main(int argc, char** argv)
{
    std::string config("view_config");
    if (argc < 4 ||argc > 5)
    {
        Path p(argv[0]);
        std::cout << "usage: " << p.getBaseName() << " <address> <port> <prefix>[view_config_file]" << std::endl;
        std::cout << "       download and display prefix.ppm from the server." << std::endl;
        return 1;
    }
    if (argc == 5)
    {
        config = argv[4];
    }

    std::string ip_addr(argv[1]);
    std::string port(argv[2]);
    std::string prefix(argv[3]);
    Logger logger;
    try
    {
        SocketAddress sa(ip_addr, port);
        StreamSocket sock(sa);
        SocketStream str(sock);

        logger.log('S', 'I', prefix);
        str << prefix << std::endl;
        Poco::UInt16 udp_port;
        sock.receiveBytes(&udp_port, sizeof(udp_port));
        std::cerr<< "pclient -- UDP port received: "<<udp_port<<std::endl;
        Ppmesh mesh(true);
        mesh.read(str, true, false);				// read the poor progressive mesh
        std::cerr<<"mesh readed."<<std::endl;
        std::cerr<<mesh.n_detail_vertices()<<std::endl;//get a number of vertex
        //initial decoding here
        int count = 0;
        str.read((char*)&count, sizeof(count));//no swap is considered.
        std::cerr<<"count "<<count<<std::endl;
        int i = 0;
        for (i = 0; i<count; i++)
        {
            BitString data;
            VertexID id;
            unsigned int len;
            size_t   p_pos = 0;
            str.read((char* )&id, sizeof(id));
            str.read((char* )&len, sizeof(len));
            //std::cerr<<"id "<<id<<" len "<<len<<std::endl;
            data.read_binary(str, len);
            //std::cerr<<"to decode "<<id<<" with "<<data<<std::endl;
            mesh.decode(id, data,&p_pos);
            //std::cerr<<i<<std::endl;
        }
        str.close();  					//reconstitution of the poor progressive mesh complete
        //======================
        PVisiblePQ visible_pq(&mesh, mesh.gfmesh(), logger);
        PRender render(argc, argv, "happy", &mesh, &visible_pq, 8, logger);

        std::ifstream ifs(config.c_str());
        if (ifs)
        {
            double dx, dy, dz, angle_x, angle_y, angle_z, scale;
            ifs>>dx>>dy>>dz>>angle_x>>angle_y>>angle_z>>scale;
            render.setView(dx,dy,dz,angle_x,angle_y,angle_z,scale);
        }

        PReceiver   receiver(mesh, visible_pq, render, ip_addr, udp_port, sock, logger);
        Thread      receiver_thread(std::string("receiver"));
        receiver_thread.start(receiver);
        render.enterMainLoop();
    }
    catch (Exception& exc)
    {
        std::cerr << exc.displayText() << std::endl;
        return 1;
    }
    return 0;
}