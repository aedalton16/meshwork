#include "scene.h"
#include "intersect.h"

#include <algorithm>

#define BVHAccelerator_min_prims 4
#define BVHAccelerator_epsilon ray3f_epsilon
#define BVHAccelerator_build_maxaxis false

// bvh accelerator node
struct BVHNode {
    bool leaf;      // leaf node
    range3f bbox;   // bounding box
#ifndef _WIN32
    union {
        struct { int start, end; }; // for leaves: start and end primitive
        struct { int n0, n1; };     // for internal: left and right node
    };
#else
    int start, end; // for leaves: start and end primitive
    int n0, n1;     // for internal: left and right node
#endif
};

// bvh accelerator
struct BVHAccelerator {
    vector<int>     prims;  // sorted primitices
    vector<BVHNode> nodes;  // bvh nodes
};

// split the list of nodes according to a policy
int make_accelerator_split(vector<pair<range3f,int>>& boxed_prims, int start, int end, const range3f& bbox, bool maxaxis) {
    auto axis = 0;
    if(maxaxis) {
        auto s = size(bbox);
        if(s.x >= s.y and s.x >= s.z) axis = 0;
        else if (s.y >= s.x and s.y >= s.z) axis = 1;
        else axis = 2;
    } else {
        auto d = zero3f;
        for(auto a : range(3)) {
            auto mid = (start+end) / 2;
            std::sort(boxed_prims.begin()+start,boxed_prims.begin()+end,
                      [a](const pair<range3f,int>& i, const pair<range3f,int>& j) {
                          return center(i.first)[a] < center(j.first)[a]; });
            auto bbox0 = range3f(), bbox1 = range3f();
            for(auto i : range(start,mid)) bbox0 = runion(bbox0, boxed_prims[i].first);
            for(auto i : range(mid,end)) bbox1 = runion(bbox1, boxed_prims[i].first);
            auto s0 = size(bbox0), s1 = size(bbox1);
            d[a] = s0.x+s0.y+s0.z+s1.x+s1.y+s1.z;
        }
        if(d.x <= d.y and d.x <= d.z) axis = 0;
        else if(d.y <= d.x and d.y <= d.z) axis = 1;
        else axis = 2;
    }
    auto mid = (start+end)/2;
    std::sort(boxed_prims.begin()+start,boxed_prims.begin()+end,
              [axis](const pair<range3f,int>& i, const pair<range3f,int>& j) {
                  return center(i.first)[axis] < center(j.first)[axis]; });
    return mid;
}

// recursively add a node to an accelerator
void make_accelerator_node(int nodeid,
                           vector<pair<range3f,int>>& boxed_prims,
                           vector<BVHNode>& nodes,
                           int start, int end) {
    range3f bbox;
    auto node = BVHNode();
    for(auto i : range(start, end)) bbox = runion(bbox,boxed_prims[i].first);
    if(end-start <= BVHAccelerator_min_prims) {
        node.bbox = bbox;
        node.leaf = true;
        node.start = start;
        node.end = end;
    } else {
        int middle = make_accelerator_split(boxed_prims,start,end,bbox,BVHAccelerator_build_maxaxis);
        node.bbox = bbox;
        node.leaf = false;
        nodes.push_back(BVHNode());
        node.n0 = nodes.size();
        nodes.push_back(BVHNode());
        node.n1 = nodes.size();
        nodes.push_back(BVHNode());
        make_accelerator_node(node.n0,boxed_prims,nodes,start,middle);
        make_accelerator_node(node.n1,boxed_prims,nodes,middle,end);
    }
    nodes[nodeid] = node;
}

// intersect bounding box
inline bool intersect_bbox(const ray3f& ray, const range3f& bbox, float& t0, float& t1) {
    t0 = ray.tmin; t1 = ray.tmax;
    for (int i = 0; i < 3; ++i) {
        auto invRayDir = 1.f / ray.d[i];
        auto tNear = (bbox.min[i] - ray.e[i]) * invRayDir;
        auto tFar  = (bbox.max[i] - ray.e[i]) * invRayDir;
        if (tNear > tFar) std::swap(tNear, tFar);
        t0 = tNear > t0 ? tNear : t0;
        t1 = tFar  < t1 ? tFar  : t1;
        if (t0 > t1) return false;
    }
    return true;
}

// intersect bounding box without returning bounds
inline bool intersect_bbox(const ray3f& ray, const range3f& bbox) {
    float t0, t1; return intersect_bbox(ray,bbox,t0,t1);
}

// intersect triangle
inline bool intersect_triangle(const ray3f& ray, const vec3f& v0, const vec3f& v1, const vec3f& v2, float& t, float& ba, float& bb) {
    auto a = v0 - v2;
    auto b = v1 - v2;
    auto e = ray.e - v2;
    auto i = ray.d;
    
    auto d = dot(cross(i,b),a);
    if(d == 0) return false;
    
    t =  dot(cross(e,a),b) / d;
    if(t < ray.tmin or t > ray.tmax) return false;
    
    ba = dot(cross(i,b),e) / d;
    bb = dot(cross(a,i),e) / d;
    if(ba < 0 or bb < 0 or ba+bb > 1) return false;
    
    return true;
}

// intersect triangle without returning bounds
inline bool intersect_triangle(const ray3f& ray, const vec3f& v0, const vec3f& v1, const vec3f& v2) {
    float t, u, v; return intersect_triangle(ray, v0, v1, v2, t, u, v);
}

// intersect sphere
inline bool intersect_sphere(const ray3f& ray, float radius, float& t) {
    auto a = lengthSqr(ray.d);
    auto b = 2*dot(ray.d,ray.e);
    auto c = lengthSqr(ray.e) - radius*radius;
    auto d = b*b-4*a*c;
    if(d < 0) return false;
    float tm = (-b-sqrt(d)) / (2*a);
    float tM = (-b+sqrt(d)) / (2*a);
    bool hitm = (tm >= ray.tmin and tm <= ray.tmax);
    bool hitM = (tM >= ray.tmin and tM <= ray.tmax);
    if(!hitm and !hitM) return false;
    t = hitm ? tm : tM;
    return true;
}

// intersect sphere without returning values
inline bool intersect_sphere(const ray3f& ray, float radius) {
    float t; return intersect_sphere(ray, radius, t);
}

// intersect quad
inline bool intersect_quad(const ray3f& ray, float radius, float& t, vec3f& p) {
    if(ray.d.z == 0) return false;
    t = - ray.e.z / ray.d.z;
    p = ray.eval(t);
    if(radius < p.x or -radius > p.x or radius < p.y or -radius > p.y) return false;
    if (t < ray.tmin or t > ray.tmax) return false;
    return true;
}

// intersect triangle without returning bounds
inline bool intersect_quad(const ray3f& ray, float radius) {
    float t; vec3f p; return intersect_quad(ray, radius, t, p);
}

// intersect an accelerator
template<typename intersect_func>
intersection3f intersect(BVHAccelerator* bvh, int nodeid, const ray3f& ray,
                         const intersect_func& intersect_elem) {
    // grab node
    auto& node = bvh->nodes[nodeid];
    // intersect bbox
    if(not intersect_bbox(ray, node.bbox)) return intersection3f();
    // recursively intersect nodes
    intersection3f intersection;
    // copy the ray to allow for shortening it
    auto sray = ray;
    if(node.leaf) {
        for(int idx = node.start; idx < node.end; idx ++) {
            auto i = bvh->prims[idx];
            intersection3f sintersection = intersect_elem(i,sray);
            if(not sintersection.hit) continue;
            if(sintersection.ray_t > intersection.ray_t and intersection.hit) continue;
            intersection = sintersection;
            sray.tmax = intersection.ray_t;
        }
    } else {
        for(auto n : { node.n0, node.n1 }) {
            intersection3f sintersection = intersect(bvh,n,sray,intersect_elem);
            if(not sintersection.hit) continue;
            if(sintersection.ray_t > intersection.ray_t and intersection.hit) continue;
            intersection = sintersection;
            sray.tmax = intersection.ray_t;
        }
    }
    return intersection;
}

// intersect an accelerator
template<typename intersect_func>
bool intersect_shadow(BVHAccelerator* bvh, int nodeid, const ray3f& ray,
                      const intersect_func& intersect_elem_shadow) {
    // grab node
    auto& node = bvh->nodes[nodeid];
    // intersect bbox
    if(not intersect_bbox(ray, node.bbox)) return false;
    // recursively intersect nodes
    if(node.leaf) {
        // for(auto idx : range(node.start,node.end)) {
        for(int idx = node.start; idx < node.end; idx++) {
            auto i = bvh->prims[idx];
            if(intersect_elem_shadow(i,ray)) return true;
        }
    } else {
        if(intersect_shadow(bvh,node.n0,ray,intersect_elem_shadow)) return true;
        if(intersect_shadow(bvh,node.n1,ray,intersect_elem_shadow)) return true;
    }
    return false;
}

// build accelerator
BVHAccelerator* make_accelerator(vector<range3f>& bboxes) {
    vector<pair<range3f,int>> boxed_prims(bboxes.size());
    for(auto i : range(bboxes.size())) boxed_prims[i] = pair<range3f,int>(rscale(bboxes[i],1+BVHAccelerator_epsilon),i);
    auto bvh = new BVHAccelerator();
    bvh->nodes.push_back(BVHNode());
    make_accelerator_node(0, boxed_prims, bvh->nodes, 0, bboxes.size());
    bvh->prims.reserve(bboxes.size());
    for(auto i : range(boxed_prims.size())) bvh->prims[i] = boxed_prims[i].second;
    return bvh;
}

// intersects the scene and return the first intrerseciton
intersection3f intersect(Scene* scene, ray3f ray) {
    // create a default intersection record to be returned
    auto intersection = intersection3f();
    // foreach surface
    for(auto surface : scene->surfaces) {
        // if it is a quad
        if(surface->isquad) {
            // compute ray intersection (and ray parameter), continue if not hit
            auto tray = transform_ray_inverse(surface->frame,ray);
            
            // intersect quad
            auto t = 0.0f; auto p = zero3f;
            auto hit = intersect_quad(tray, surface->radius, t, p);
            
            // skip if not hit
            if(not hit) continue;
            
            // check if this is the closest intersection, continue if not
            if(t > intersection.ray_t and intersection.hit) continue;
            
            // if hit, set intersection record values
            intersection.hit = true;
            intersection.ray_t = t;
            intersection.pos = transform_point(surface->frame,p);
            intersection.norm = transform_normal(surface->frame,z3f);
            intersection.texcoord = {0.5f*p.x/surface->radius+0.5f,0.5f*p.y/surface->radius+0.5f};
            intersection.mat = surface->mat;
        } else {
            // compute ray intersection (and ray parameter), continue if not hit
            auto tray = transform_ray_inverse(surface->frame,ray);
            
            // intersect sphere
            auto t = 0.0f;
            auto hit = intersect_sphere(tray, surface->radius, t);
            
            // skip if not hit
            if(not hit) continue;
            
            // check if this is the closest intersection, continue if not
            if(t > intersection.ray_t and intersection.hit) continue;
            
            // compute local point and normal
            auto p = tray.eval(t);
            auto n = normalize(p);
            
            // if hit, set intersection record values
            intersection.hit = true;
            intersection.ray_t = t;
            intersection.pos = transform_point(surface->frame,p);
            intersection.norm = transform_normal(surface->frame,n);
            intersection.texcoord = {(pif+(float)atan2(n.y, n.x))/(2*pif),(float)acos(n.z)/pif};
            intersection.mat = surface->mat;
        }
    }
    // foreach mesh
    for(auto mesh : scene->meshes) {
        // quads are not supported: check for error
        error_if_not(mesh->quad.empty(), "quad intersection is not supported");
        // tranform the ray
        auto tray = transform_ray_inverse(mesh->frame, ray);
        // save auto mesh intersection
        auto sintersection = intersection3f();
        // if it is accelerated
        if(mesh->bvh) {
            sintersection = intersect(mesh->bvh, 0, tray,
               [mesh](int tid, ray3f tray){
                   // grab triangle
                   auto triangle = mesh->triangle[tid];
                   
                   // grab vertices
                   auto v0 = mesh->pos[triangle.x];
                   auto v1 = mesh->pos[triangle.y];
                   auto v2 = mesh->pos[triangle.z];
                   
                   // intersect triangle
                   auto t = 0.0f, u = 0.0f, v = 0.0f;
                   auto hit = intersect_triangle(tray, v0, v1, v2, t, u, v);
                   
                   // skip if not hit
                   if(not hit) return intersection3f();
                   
                   // if hit, set up intersection, trasforming hit data to world space
                   auto sintersection = intersection3f();
                   sintersection.hit = true;
                   sintersection.ray_t = t;
                   sintersection.pos = tray.eval(t);
                   sintersection.norm = normalize(mesh->norm[triangle.x]*u+
                                                  mesh->norm[triangle.y]*v+
                                                  mesh->norm[triangle.z]*(1-u-v));
                   if(mesh->texcoord.empty()) sintersection.texcoord = zero2f;
                   else {
                       sintersection.texcoord = mesh->texcoord[triangle.x]*u+
                                                mesh->texcoord[triangle.y]*v+
                                                mesh->texcoord[triangle.z]*(1-u-v);
                   }
                   sintersection.mat = mesh->mat;
                   return sintersection;
               });
        } else {
            // clear intersection
            sintersection = intersection3f();
            // foreach triangle
            for(auto triangle : mesh->triangle) {
                // grab vertices
                auto v0 = mesh->pos[triangle.x];
                auto v1 = mesh->pos[triangle.y];
                auto v2 = mesh->pos[triangle.z];
                
                // intersect triangle
                auto t = 0.0f, u = 0.0f, v = 0.0f;
                auto hit = intersect_triangle(tray, v0, v1, v2, t, u, v);
                
                // skip if not hit
                if(not hit) continue;
                
                // check if closer then the found hit
                if(t > sintersection.ray_t and sintersection.hit) continue;
                
                // if hit, set up intersection, trasforming hit data to world space
                sintersection.hit = true;
                sintersection.ray_t = t;
                sintersection.pos = tray.eval(t);
                sintersection.norm = normalize(mesh->norm[triangle.x]*u+
                                               mesh->norm[triangle.y]*v+
                                               mesh->norm[triangle.z]*(1-u-v));
                if(mesh->texcoord.empty()) sintersection.texcoord = zero2f;
                else {
                    sintersection.texcoord = mesh->texcoord[triangle.x]*u+
                                             mesh->texcoord[triangle.y]*v+
                                             mesh->texcoord[triangle.z]*(1-u-v);
                }
                sintersection.mat = mesh->mat;
            }
        }
        // if did not hit the mesh, skip
        if(not sintersection.hit) continue;
        // check not first intersection, skip
        if(sintersection.ray_t > intersection.ray_t and intersection.hit) continue;
        // set interserction
        intersection = sintersection;
        // transform by mesh frame
        intersection.pos = transform_point(mesh->frame,sintersection.pos);
        intersection.norm = transform_normal(mesh->frame,sintersection.norm);
        // set material
        intersection.mat = sintersection.mat;
    }
    
    // record closest intersection
    return intersection;
}

// intersects the scene and return for any intersection
bool intersect_shadow(Scene* scene, ray3f ray) {
    // foreach surface
    for(auto surface : scene->surfaces) {
        // if it is a quad
        if(surface->isquad) {
            // compute ray intersection (and ray parameter), continue if not hit
            auto tray = transform_ray_inverse(surface->frame,ray);
            
            // intersect quad
            if(intersect_quad(tray, surface->radius)) return true;
        } else {
            // compute ray intersection (and ray parameter), continue if not hit
            auto tray = transform_ray_inverse(surface->frame,ray);
            
            // intersect sphere
            if(intersect_sphere(tray, surface->radius)) return true;
        }
    }
    // foreach mesh
    for(auto mesh : scene->meshes) {
        // quads are not supported: check for error
        error_if_not(mesh->quad.empty(), "quad intersection is not supported");
        // tranform the ray
        auto tray = transform_ray_inverse(mesh->frame, ray);
        // if it is accelerated
        if(mesh->bvh) {
            if(intersect_shadow(mesh->bvh, 0, tray,
                                [mesh](int tid, ray3f tray){
                                    // grab triangle
                                    auto triangle = mesh->triangle[tid];
                                          
                                    // grab vertices
                                    auto v0 = mesh->pos[triangle.x];
                                    auto v1 = mesh->pos[triangle.y];
                                    auto v2 = mesh->pos[triangle.z];
                            
                                    // return if intersected
                                    return intersect_triangle(tray, v0, v1, v2);})) return true;
        } else {
            // foreach triangle
            for(auto triangle : mesh->triangle) {
                // grab vertices
                auto v0 = mesh->pos[triangle.x];
                auto v1 = mesh->pos[triangle.y];
                auto v2 = mesh->pos[triangle.z];
                
                // intersect triangle
                if(intersect_triangle(tray, v0, v1, v2)) return true;
            }
        }
    }
    
    // no intersection found
    return false;
}

// prepare scene acceleration and triangulate meshes
void accelerate(Scene* scene) {
    // foreach mesh, init bvh acceleration structure to nullptr
    for(auto mesh : scene->meshes) mesh->bvh = nullptr;
    
    // if scene should be accelerated using bvh
    if(scene->accelerate_bvh) {
        // foreach mesh
        for (auto mesh : scene->meshes) {
            // acceleration structure does not support animations
            if(mesh->animation) continue;
            
            // triangulate quads (convert all quads into two tris)
            for(auto f : mesh->quad) {
                mesh->triangle.push_back({f.x,f.y,f.z});
                mesh->triangle.push_back({f.x,f.z,f.w});
            }
            // clear out quads vector
            mesh->quad.clear();
            
            // make acceleration structure
            // check whether to accelerate
            if (mesh->triangle.size()+mesh->quad.size() > BVHAccelerator_min_prims) {
                // grab all bbox
                auto bboxes = vector<range3f>(mesh->triangle.size());
                for(auto i : range(mesh->triangle.size())) {
                    auto f = mesh->triangle[i];
                    bboxes[i] = make_range3f({mesh->pos[f.x],mesh->pos[f.y],mesh->pos[f.z]});
                }
                // make accelerator
                mesh->bvh = make_accelerator(bboxes);
            }
        }
    }
}

