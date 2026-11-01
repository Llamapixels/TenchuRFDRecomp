// Included inside tenchu::cloth after Frame is declared.
struct Edge {uint16_t a=0,b=0;};
struct Topology {
    unsigned count=0;float spacing=1;
    std::array<Edge,kMaxPoints*6> edges{};
    std::array<std::bitset<kMaxPoints>,kMaxPoints> local{};
};
inline Topology MakeTopology(const Frame& f){
    Topology t;float spacing=1.e30f;
    for(unsigned i=0;i<f.count;i++)for(unsigned e=0;e<6;e++){
        const auto j=f.points[i].neighbor[e];if(j==0xffff||j>=f.count||j==i)continue;
        if(f.points[i].length[e]>1.e-4f)spacing=std::min(spacing,f.points[i].length[e]);
        if(!t.local[i].test(j))t.edges[t.count++]={uint16_t(i),j};
        t.local[i].set(j);t.local[j].set(i);
    }
    const auto direct=t.local;
    for(unsigned i=0;i<f.count;i++){
        t.local[i].set(i);
        for(unsigned j=0;j<f.count;j++)if(direct[i].test(j))t.local[i]|=direct[j];
    }
    t.spacing=spacing<1.e29f?spacing:1;return t;
}
inline uint64_t ShapeSignature(const Frame& f){
    uint64_t hash=1469598103934665603ull;
    auto add=[&](uint32_t v){hash=(hash^v)*1099511628211ull;};
    add(f.count);add(f.grid);add(f.width);
    for(unsigned i=0;i<f.count;i++)for(unsigned e=0;e<6;e++){
        add(f.points[i].neighbor[e]);add(std::bit_cast<uint32_t>(f.points[i].length[e]));
    }
    return hash;
}
inline RestShape CaptureShape(const Frame& f){
    RestShape rest;
    if(!Valid(f))return rest;
    rest.points=f.count;rest.signature=ShapeSignature(f);
    auto add=[&](unsigned a,unsigned b){
        if(a>b)std::swap(a,b);
        if(a==b||rest.count>=rest.bends.size())return;
        for(unsigned i=0;i<rest.count;i++)if(rest.bends[i].a==a&&rest.bends[i].b==b)return;
        const float length=Length(f.points[b].position-f.points[a].position);
        if(length>1.e-5f&&std::isfinite(length))rest.bends[rest.count++]={uint16_t(a),uint16_t(b),length};
    };
    if(f.grid){
        for(unsigned i=0;i<f.count;i++){
            if(i%f.width+2<f.width)add(i,i+2);
            if(i+2*f.width<f.count)add(i,i+2*f.width);
        }
    }else{
        for(unsigned middle=0;middle<f.count;middle++){
            for(unsigned ea=0;ea<6;ea++){
                const unsigned a=f.points[middle].neighbor[ea];if(a>=f.count||a==middle)continue;
                for(unsigned eb=ea+1;eb<6;eb++){
                    const unsigned b=f.points[middle].neighbor[eb];if(b>=f.count||b==middle)continue;
                    const Vec x=f.points[middle].position-f.points[a].position,y=f.points[b].position-f.points[middle].position;
                    if(Dot(x,y)>0)add(a,b);
                }
            }
        }
    }
    return rest;
}
inline void ApplyBending(Frame& f,float strength){
    if(strength<=0||!f.rest||f.rest->points!=f.count||f.rest->count>f.rest->bends.size())return;
    for(unsigned i=0;i<f.rest->count;i++){
        const auto& link=f.rest->bends[i];if(link.a>=f.count||link.b>=f.count)continue;
        auto& a=f.points[link.a];auto& b=f.points[link.b];
        const Vec delta=b.position-a.position;const float length=Length(delta);
        const float weight=(!a.pinned)+(!b.pinned);
        if(length<1.e-6f||weight==0)continue;
        const Vec correction=delta*((length-link.length)/length*strength/weight);
        if(!a.pinned)a.position=a.position+correction;
        if(!b.pinned)b.position=b.position-correction;
    }
}
inline Vec Cross(Vec a,Vec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline Vec ContactNormal(Vec delta,Vec previous,Vec axis){
    float length=Length(delta);if(length>1.e-6f)return delta*(1/length);
    length=Length(previous);if(length>1.e-6f)return previous*(1/length);
    const Vec fallback=std::fabs(axis.x)>std::fabs(axis.z)?Vec{-axis.y,axis.x,0}:Vec{0,-axis.z,axis.y};
    length=Length(fallback);return length>1.e-6f?fallback*(1/length):Vec{1,0,0};
}
struct SegmentPair {Vec a,b;float s=0,t=0;};
inline SegmentPair ClosestSegments(Vec a,Vec b,Vec c,Vec d){
    const Vec u=b-a,v=d-c,r=a-c;
    const float aa=Dot(u,u),ee=Dot(v,v),ff=Dot(v,r);
    float s=0,t=0;
    if(aa<=1.e-8f){if(ee>1.e-8f)t=std::clamp(ff/ee,0.f,1.f);}
    else{
        const float cc=Dot(u,r);
        if(ee<=1.e-8f)s=std::clamp(-cc/aa,0.f,1.f);
        else{
            const float bb=Dot(u,v),denom=aa*ee-bb*bb;
            if(denom>1.e-8f)s=std::clamp((bb*ff-cc*ee)/denom,0.f,1.f);
            t=(bb*s+ff)/ee;
            if(t<0){t=0;s=std::clamp(-cc/aa,0.f,1.f);}
            else if(t>1){t=1;s=std::clamp((bb-cc)/aa,0.f,1.f);}
        }
    }
    return {a+u*s,c+v*t,s,t};
}
inline bool BoundsOverlap(Vec a,Vec b,Vec c,Vec d,float gap){
    return std::max(a.x,b.x)+gap>=std::min(c.x,d.x)&&std::max(c.x,d.x)+gap>=std::min(a.x,b.x)&&
        std::max(a.y,b.y)+gap>=std::min(c.y,d.y)&&std::max(c.y,d.y)+gap>=std::min(a.y,b.y)&&
        std::max(a.z,b.z)+gap>=std::min(c.z,d.z)&&std::max(c.z,d.z)+gap>=std::min(a.z,b.z);
}
inline void CollideEdges(Frame& f,const Topology& topology,const std::array<Vec,kMaxPoints>& previous,
                         std::array<Vec,kMaxPoints>& contact,std::array<bool,kMaxPoints>& touching){
    for(unsigned e=0;e<topology.count;e++){
        const auto edge=topology.edges[e];auto& a=f.points[edge.a];auto& b=f.points[edge.b];
        if(a.pinned&&b.pinned)continue;
        for(unsigned c=0;c<f.body.count;c++){
            const auto& capsule=f.body.capsules[c];
            if(!BoundsOverlap(a.position,b.position,capsule.a,capsule.b,capsule.radius))continue;
            const auto pair=ClosestSegments(a.position,b.position,capsule.a,capsule.b);
            const float distance=Length(pair.a-pair.b);if(distance>=capsule.radius)continue;
            const float wa=a.pinned?0:1-pair.s,wb=b.pinned?0:pair.s,weight=wa*wa+wb*wb;
            if(weight<1.e-6f)continue;
            const Vec old=previous[edge.a]*(1-pair.s)+previous[edge.b]*pair.s;
            const Vec normal=ContactNormal(pair.a-pair.b,old-Closest(old,capsule),capsule.b-capsule.a);
            const float push=std::min((capsule.radius-distance+.001f)*f.edges/weight,capsule.radius*2);
            if(!a.pinned){a.position=a.position+normal*(push*wa);contact[edge.a]=normal;touching[edge.a]=true;}
            if(!b.pinned){b.position=b.position+normal*(push*wb);contact[edge.b]=normal;touching[edge.b]=true;}
        }
    }
}
inline void SeparateSelf(Frame& f,const Topology& topology,const std::array<Vec,kMaxPoints>& previous,
                         std::array<Vec,kMaxPoints>& contact,std::array<bool,kMaxPoints>& touching){
    const float gap=std::clamp(topology.spacing*f.thickness,.0001f,100.f);
    for(unsigned a=0;a<f.count;a++)for(unsigned b=a+1;b<f.count;b++){
        if(topology.local[a].test(b))continue;
        auto& x=f.points[a];auto& y=f.points[b];const float weight=(!x.pinned)+(!y.pinned);
        const Vec delta=x.position-y.position;const float distance=Length(delta);
        if(!weight||distance>=gap)continue;
        const Vec normal=ContactNormal(delta,previous[a]-previous[b],{0,1,0});
        const Vec push=normal*((gap-distance)*f.self/weight);
        if(!x.pinned){x.position=x.position+push;contact[a]=normal;touching[a]=true;}
        if(!y.pinned){y.position=y.position-push;contact[b]=normal*(-1);touching[b]=true;}
    }
    for(unsigned a=0;a<topology.count;a++)for(unsigned b=a+1;b<topology.count;b++){
        const auto x=topology.edges[a],y=topology.edges[b];
        if(topology.local[x.a].test(y.a)||topology.local[x.a].test(y.b)||
           topology.local[x.b].test(y.a)||topology.local[x.b].test(y.b))continue;
        auto& p=f.points[x.a];auto& q=f.points[x.b];auto& r=f.points[y.a];auto& s=f.points[y.b];
        if(!BoundsOverlap(p.position,q.position,r.position,s.position,gap))continue;
        const auto pair=ClosestSegments(p.position,q.position,r.position,s.position);
        const float distance=Length(pair.a-pair.b);if(distance>=gap)continue;
        const float wa=p.pinned?0:1-pair.s,wb=q.pinned?0:pair.s,wc=r.pinned?0:1-pair.t,wd=s.pinned?0:pair.t;
        const float weight=wa*wa+wb*wb+wc*wc+wd*wd;if(weight<1.e-6f)continue;
        const Vec old=previous[x.a]*(1-pair.s)+previous[x.b]*pair.s-previous[y.a]*(1-pair.t)-previous[y.b]*pair.t;
        const Vec fallback=Length(old)>1.e-6f?old:Cross(q.position-p.position,s.position-r.position);
        const Vec normal=ContactNormal(pair.a-pair.b,fallback,q.position-p.position);
        const float push=std::min((gap-distance)*f.self/weight,gap*2);
        auto move=[&](unsigned i,float w,float sign){if(w){f.points[i].position=f.points[i].position+normal*(push*w*sign);contact[i]=normal*sign;touching[i]=true;}};
        move(x.a,wa,1);move(x.b,wb,1);move(y.a,wc,-1);move(y.b,wd,-1);
    }
}
