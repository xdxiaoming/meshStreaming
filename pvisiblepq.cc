#include "vertexid.hh"
#include "pvisiblepq.hh"
#include "Poco/RunnableAdapter.h"
#include "Poco/Mutex.h"
#include "Poco/Exception.h"
#include "gfmesh.hh"
static Ppmesh* ppmesh_ = NULL;
static Gfmesh* gfmesh_ = NULL;


PVisiblePQ::PVisiblePQ(Ppmesh* ppmesh, Gfmesh* gfmesh, Logger& logger)
        :toContinue_(true), isStrict_(true), logger_(logger),\
        toQuit_(false), pixels_(0), size_(0) //ppmesh_(ppmesh), gfmesh_(gfmesh)
{
    //to_run_ = new Poco::RunnableAdapter<PVisiblePQ>(*this, &PVisiblePQ::run);
    ppmesh_ = ppmesh;
    gfmesh_ = gfmesh;
}

PVisiblePQ::~PVisiblePQ(void)
    {
        //if (in_queue_) delete in_queue_;
        //if (out_queue_) delete out_queue_;
        //delete to_run_;
    }

struct PacketInfo
{
    bool isSent;
    int  area;
    PacketInfo():isSent(false), area(0){}
};
    
void PVisiblePQ::stop(void)
    {
        toContinue_ = false;
    }

    void PVisiblePQ::quit(void)
    {
        toQuit_ = true;
        toContinue_ =false;
    }

//Update the packet importance according to the pixels_ buffer.
void PVisiblePQ::stat_screen_area()
{
    Gfmesh* gfmesh = gfmesh_;
    //clear weight
    for (int i = 0; i < 10; i++)
    {
        pid_lists[i].clear();
    }
    gfmesh->clear_weight();
    p_id_set_.clear();
    for (size_t i = 0; i < size_; i+=3)
    {
        unsigned char color_r = pixels_[i];
        unsigned char color_g = pixels_[i+1];
        unsigned char color_b = pixels_[i+2];
        Index seq_no = color_r * 65536 + color_g*256 + color_b;
        if (seq_no != 0)
        {
            gfmesh->increment_face_weight(seq_no-1);
        }
        if (!toContinue_) return;
    }
    for (size_t i = 0; i < gfmesh->face_number(); i++)
    {
        if (gfmesh_->face_weight(i) == 0)
        {
            //gfmesh_->set_visibility(i, false);
            //cannot set to false. Otherwise, some old hide faces now visible cannot be detected.
        }
        else
        {
            gfmesh_->set_visibility(i, true);
            gfmesh->add_vertex_weight_in_face(i);
        }
        if (!toContinue_) return;
    }

    //Now we compute the packet weight from the vertex weight.
    std::vector<VertexID>::const_iterator it(ppmesh_->vertex_front().begin());
    std::vector<VertexID>::const_iterator end(ppmesh_->vertex_front().end());
    
    //add a local map to increase the efficiency.
    std::map<PacketID, PacketInfo> local_info;
    std::map<PacketID, PacketInfo>::iterator local_iter;
    std::map<PacketID, PacketInfo>::iterator local_end = local_info.end();
    for (; it != end; ++it)
    {
        if (!toContinue_) break;
        if (ppmesh_->id2level(*it) < 5) continue;
        if (ppmesh_->idIsLeaf(*it)) continue;
        if (ppmesh_->isPicked(*it)) continue;
        if (isStrict_)
        {
            if (gfmesh_->vertex_weight(ppmesh_->id2index(*it)) == 0) continue;
        }
        PacketID p_id = v_id_to_p_id(*it, *ppmesh_);
        local_iter = local_info.find(p_id);
        if (local_iter != local_end)
        {
            if (local_iter->second.isSent) continue;
            local_iter->second.area += gfmesh_->vertex_weight(ppmesh_->id2index(*it));
        }
        else
        {
            if (p_id_sent_[p_id])
            {
                local_info[p_id].isSent = true;
                continue;
            }
            else
            {
                local_info[p_id].area = gfmesh_->vertex_weight(ppmesh_->id2index(*it));
                p_id_set_.insert(p_id); //add to sent packets set.
            }
        }
    }
    std::set<PacketID>::const_iterator pit(p_id_set_.begin());
    std::set<PacketID>::const_iterator pend(p_id_set_.end());
    for (; pit != pend; ++pit)
    {
        if (!toContinue_) break;
        push(*pit, local_info[*pit].area);
    }

}

void PVisiblePQ::update(unsigned char* pixels, size_t size)
{

    //toContinue_=false;
    //stoped_.lock();
    pixels_ = pixels;
    size_ = size;
    //toContinue_=true;
    //if (in_queue_) delete in_queue_;
    //in_queue_ = new PQ();
    //weights.reset();
    //std::cerr<<thread_pool_.available()<<" "<<thread_pool_.allocated()<<std::endl;
    //thread_pool_.start(*to_run_);
    run();
}

void PVisiblePQ::push(PacketID pid, int area)
{
    if (area <= 1)
    {
        pid_lists[0].push_back(pid);
    }
    else if (area == 2)
    {
        pid_lists[1].push_back(pid);
    }
    else if (area <=4 )
    {
        pid_lists[2].push_back(pid);
    }
    else if (area<=8)
    {
        pid_lists[3].push_back(pid);
    }
    else if (area<=16)
    {
        pid_lists[4].push_back(pid);
    }
    else if (area <= 32)
    {
        pid_lists[5].push_back(pid);
    }
    else if (area <= 64)
    {
        pid_lists[6].push_back(pid);
    }
    else if (area <= 128)
    {
        pid_lists[7].push_back(pid);
    }
    else if (area <= 256)
    {
        pid_lists[8].push_back(pid);
    }
    else
    {
        pid_lists[9].push_back(pid);
    }
}

PacketID PVisiblePQ::pop()
{
    PacketID top = 0;
    //we try to find a balance between send from the PQ and send from the to_be_split_.
    //So we introduce a flag to choose one for one time.
    static bool fromPQ = true;
    if (!fromPQ)
    {
        while (top == 0)
        {
            if (toQuit_)
            {
                stoped_.lock();
                exit(0);
            }
            VertexID id = ppmesh_->pick_one();
            if (id == 0) break;
            top = v_id_to_p_id(id, *ppmesh_);
            if (p_id_sent_[top])
            {
                top = 0;
            }
        }
        fromPQ = true;
    }
    if (fromPQ || top == 0)
    {
        if (toQuit_)
        {
            stoped_.lock();
            exit(0);
        }
        for (int i = 9; i >=0; i--)
        {
            if (toQuit_)
            {
                stoped_.lock();
                exit(0);
            }
            if (!pid_lists[i].empty())
            {
                top = pid_lists[i].front();
                pid_lists[i].pop_front();
                assert(top!=0);
                if (!p_id_sent_[top]) break;
            }
        }
        if (fromPQ) fromPQ = false;
    }
    p_id_sent_[top] = true;
    return top;
}

void PVisiblePQ::run()
{
    toContinue_=false;
    stoped_.lock();
    toContinue_=true;
    stat_screen_area();
    //std::cerr<<"out queue size "<<out_queue_->size()<<std::endl;
    stoped_.unlock();
}















