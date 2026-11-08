#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <vector>

namespace dkr::runtime::vehicles::lighting {
struct Vec { float x=0, y=0, z=0; };
using Matrix = std::array<std::array<float,3>,3>;
using Corners = std::array<Vec,3>;
inline float dot(Vec a, Vec b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec unit(Vec v) {
    const auto length=std::sqrt(dot(v,v));
    if (!std::isfinite(length) || length<1e-12F) return {};
    return {v.x/length,v.y/length,v.z/length};
}
inline Vec transform(Vec v, const Matrix& m) {
    return {v.x*m[0][0]+v.y*m[1][0]+v.z*m[2][0],
            v.x*m[0][1]+v.y*m[1][1]+v.z*m[2][1],
            v.x*m[0][2]+v.y*m[1][2]+v.z*m[2][2]};
}
inline Vec yaw(Vec v, float angle) {
    const float c=std::cos(angle),s=std::sin(angle);
    return {v.x*c+v.z*s,v.y,-v.x*s+v.z*c};
}
inline Matrix normal_matrix(const Matrix& m) {
    // Row-vector inverse transpose. Translation and projection never enter it.
    const float det=m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-
        m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])+
        m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
    if (!std::isfinite(det) || std::abs(det)<1e-20F)
        throw std::runtime_error("Custom vehicle lighting has a singular model transform.");
    Matrix result{};
    for (unsigned r=0;r<3;++r) for (unsigned c=0;c<3;++c) {
        const auto a=(r+1)%3,b=(r+2)%3,d=(c+1)%3,e=(c+2)%3;
        result[r][c]=(m[a][d]*m[b][e]-m[a][e]*m[b][d])/det;
        if (!std::isfinite(result[r][c]))
            throw std::runtime_error("Custom vehicle lighting has a non-finite normal transform.");
    }
    return result;
}

template<class Vertices, class Faces>
std::vector<Corners> normals(const Vertices& vertices, const Faces& faces) {
    // Weld exact positions within one authored mesh, including UV seams.
    // Face-corner normals preserve creases of 45 degrees or more. Generate
    // BEFORE texture clipping, which otherwise changes adjacency/weights.
    struct Neighbour { unsigned face; float angle; };
    std::map<std::array<float,3>,std::vector<Neighbour>> neighbours;
    std::vector<Vec> directions(faces.size());
    const auto point=[&](unsigned index) {
        const auto& v=vertices.at(index); return Vec{v.x,v.y,v.z};
    };
    const auto subtract=[](Vec a,Vec b){return Vec{a.x-b.x,a.y-b.y,a.z-b.z};};
    for (unsigned f=0;f<faces.size();++f) {
        const auto a=point(faces[f][0]),b=point(faces[f][1]),c=point(faces[f][2]);
        const auto u=subtract(b,a),v=subtract(c,a);
        directions[f]=unit({u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x});
        if (dot(directions[f],directions[f])==0) continue;
        for (unsigned k=0;k<3;++k) {
            const auto p=point(faces[f][k]);
            const auto left=unit(subtract(point(faces[f][(k+1)%3]),p));
            const auto right=unit(subtract(point(faces[f][(k+2)%3]),p));
            neighbours[{p.x,p.y,p.z}].push_back({f,std::acos(std::clamp(dot(left,right),-1.F,1.F))});
        }
    }
    std::vector<Corners> result(faces.size());
    for (unsigned f=0;f<faces.size();++f) for (unsigned k=0;k<3;++k) {
        const auto p=point(faces[f][k]); Vec sum{};
        const auto found=neighbours.find({p.x,p.y,p.z});
        if (found!=neighbours.end()) for (const auto& adjacent:found->second) {
            const auto n=directions[adjacent.face];
            if (dot(directions[f],n)<0.7071067F) continue;
            sum.x+=n.x*adjacent.angle;sum.y+=n.y*adjacent.angle;sum.z+=n.z*adjacent.angle;
        }
        result[f][k]=unit(sum);
    }
    return result;
}

struct Sample {
    float exposure=1,ambient=1,diffuse=0;
    Vec direction{};
    std::uint32_t environment=0,flags=0;
    bool enabled() const { return (flags&1U)!=0; }
    bool fast() const { return (flags&2U)!=0; }
};
// Eight bounded display-list words. Never consult later guest globals on the
// decoder thread: another racer/view or a rollback replay may have changed them.
class Stream {
public:
    Sample sample{};
    bool put(unsigned field,std::uint32_t bits) {
        if (field>=8 || (seen_&(1U<<field))) return false;
        if (field<6) {
            const auto value=std::bit_cast<float>(bits);
            if (!std::isfinite(value) || std::abs(value)>16.F || (field<3&&value<0)) return false;
            switch(field) {
                case 0: sample.exposure=value;break;
                case 1: sample.ambient=value;break;
                case 2: sample.diffuse=value;break;
                case 3: sample.direction.x=value;break;
                case 4: sample.direction.y=value;break;
                case 5: sample.direction.z=value;break;
            }
        } else if (field==6) sample.environment=bits;
        else { if (bits&~3U) return false; sample.flags=bits; }
        seen_|=1U<<field;return true;
    }
    bool ready() const { return seen_==255; }
private:
    unsigned seen_=0;
};
inline float brightness(const Sample& s,Vec normal) {
    if (!s.enabled()) return 1;
    // Retail calc_dynamic_lighting_for_object_2 uses a 2x diffuse dot;
    // obj_shade_fast uses 160/255 as both its floor and diffuse strength.
    const auto incidence=std::max(0.F,dot(normal,s.direction));
    const auto value=s.fast() ? (160.F/255.F)*s.exposure*(1+incidence) :
        s.exposure*(s.ambient+2.F*s.diffuse*incidence);
    return std::clamp(value,0.F,1.F);
}
inline std::array<std::uint8_t,3> shade(const Sample& s,Vec normal,
                                     const std::array<std::uint8_t,4>& colour) {
    const auto strength=brightness(s,normal);
    return {std::uint8_t(std::lround(colour[0]*strength)),
            std::uint8_t(std::lround(colour[1]*strength)),
            std::uint8_t(std::lround(colour[2]*strength))};
}

// Cycle 0: texture * lit material RGB, alpha = texture * primitive opacity.
// Cycle 1: retail environment-colour blend; alpha passes through unchanged.
// Fog may replace shade alpha; it must NEVER become vehicle opacity.
inline constexpr std::uint64_t combine(bool textured) {
    const std::uint32_t low=(textured ? (1U<<20)|(4U<<15)|(1U<<12)|(3U<<9) :
        (15U<<20)|(31U<<15)|(7U<<12)|(7U<<9)) | (5U<<5)|12U;
    const std::uint32_t high=(8U<<28)|(7U<<21)|(7U<<18)|
        ((textured?7U:4U)<<15)|(7U<<12)|((textured?7U:3U)<<9)|(7U<<3);
    return (std::uint64_t(high)<<32)|low;
}
}
