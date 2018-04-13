#include "PrimoMeshViewer.h"
#include <Eigen/Sparse>
#include <unordered_set>
typedef Eigen::SparseMatrix<float> SpMat;
typedef OpenMesh::TriMesh_ArrayKernelT<>  Mesh;
struct PijKey{
    int p[2];
    PijKey(const int &a, const int &b){
        p[0] = a;
        p[1] = b;
    }
    int& operator[](unsigned int i) { return p[i]; }
    const int& operator[](unsigned int i) const { return p[i]; }
    bool operator==(const PijKey &other) const
    {
        return (p[0] == other.p[0]
            && p[1] == other.p[1]
            );
    }
};
namespace std {

  template <>
  struct hash<PijKey>{
    // boost::hash_combine for pair int's hash
    std::size_t operator()(const PijKey& p) const{
        size_t seed = 0;
        hash<int> h;
        seed ^= h(p[0]) + 0x9e3779b9 + (seed << 6) + (seed >> 2); 
        seed ^= h(p[1]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
  };

}
// static void fill_in_D_T(const OpenMesh::Vec3f &p_ij, 
//                         const int f_i_id, bool is_filling_i, SpMat &D_T){
//     //helper function to fill in D_ab and D_cd, refer to ZJW's note
//     const int i_startId = f_i_id * 6;
//     const OpenMesh::Vec3f p = is_filling_i ? p_ij : -p_ij;
//     const int one = is_filling_i ? 1 : -1;
//     // const int j_startId = f_j_id * 6;
//     // fill in first column
//     D_T.insert(i_startId + 1, 0) =  p[2];
//     D_T.insert(i_startId + 2, 0) = -p[1];
//     D_T.insert(i_startId + 3, 0) =  one;
//     D_T.insert(i_startId + 4, 0) =  one;
//     D_T.insert(i_startId + 5, 0) =  one;

//     // D_T.insert(j_startId + 1, 0) = -p_ji[2];
//     // D_T.insert(j_startId + 2, 0) =  p_ji[1];
//     // D_T.insert(j_startId + 3, 0) = -1;
//     // D_T.insert(j_startId + 4, 0) = -1;
//     // D_T.insert(j_startId + 5, 0) = -1;

//     // fill in second column
//     D_T.insert(i_startId    , 1) = -p[2];
//     D_T.insert(i_startId + 2, 1) =  p[0];
//     D_T.insert(i_startId + 3, 1) =  one;
//     D_T.insert(i_startId + 4, 1) =  one;
//     D_T.insert(i_startId + 5, 1) =  one;

//     // D_T.insert(j_startId    , 1) =  p_ji[2];
//     // D_T.insert(j_startId + 2, 1) = -p_ji[0];
//     // D_T.insert(j_startId + 3, 1) = -1;
//     // D_T.insert(j_startId + 4, 1) = -1;
//     // D_T.insert(j_startId + 5, 1) = -1;

//     // fill in third column
//     D_T.insert(i_startId    , 2) =  p[1];
//     D_T.insert(i_startId + 1, 2) = -p[0];
//     D_T.insert(i_startId + 3, 2) =  one;
//     D_T.insert(i_startId + 4, 2) =  one;
//     D_T.insert(i_startId + 5, 2) =  one;

//     // D_T.insert(j_startId    , 2) = -p_ji[1];
//     // D_T.insert(j_startId + 1, 2) =  p_ji[0];
//     // D_T.insert(j_startId + 3, 2) = -1;
//     // D_T.insert(j_startId + 4, 2) = -1;
//     // D_T.insert(j_startId + 5, 2) = -1;
// }

// helper class to memoization for pij and pji
class Pij_ji{
public:
    explicit Pij_ji(const PrismProperty * const P_i, const PrismProperty * const P_j):
    f_ij{       {P_i->FromVertPrismDown, P_i->FromVertPrismUp},
                {P_i->ToVertPrismDown, P_i->ToVertPrismUp}
    },
    f_ji{       {P_j->ToVertPrismDown, P_j->ToVertPrismUp},
                {P_j->FromVertPrismDown, P_j->FromVertPrismUp}
    }, one{
        {{1, 1, 1}, {1, 1, 1}},
        {{1, 1, 1}, {1, 1, 1}}
    }
    {}
    float operator()(const int first, const int second){ 
        // assert it is a valid key
        // pij(x, y, z)   pji(x, y, z)  one(1,  1,  1)
        //     0  1  2        3  4  5      -1  -1  -1
        assert(first >= -1 && first <= 5 && second >= -1 && second <= 5);
        // firstly check if the key is already calculated
        PijKey key{first, second};
        auto iter = p.find(key);
        if(iter != p.end()){
            // this pair has already been calculated, return directly
            return iter->second;
        }else{
            // the (key,value) is not in the map
            // 1. calculate it 
            float value = calc_value(key[0], key[1]);
            // 2. save it to the map
            p[key] = value;
            if(key[1] != key[0]){
                p[{key[1], key[0]}] = value;
            }
            // return it
            return value;
        }

    }
    // private data
private:
    std::unordered_map<PijKey, float> p;
    const OpenMesh::Vec3f f_ij[2][2];
    const OpenMesh::Vec3f f_ji[2][2];
    // dummy one vectors for easy implementation
    const OpenMesh::Vec3f one[2][2];
    //
    float calc_value(int first, int second) const{
        // firstly sort two integer small, big
        if(first > second){
            std::swap(first, second);
        }
        // cannot have two "one"
        assert(second >= 0);
        // special case when first is -1
        // 10 non-repetitive combination for the diecretion of integration over [0, 1]^[0, 1] 
        static const int uv_integrate_id[10][4] = {
            {0, 0, 0, 0},
            {0, 0, 0, 1},
            {0, 0, 1, 0},
            {0, 0, 1, 1},
            {0, 1, 0, 1},
            {0, 1, 1, 0},
            {0, 1, 1, 1},
            {1, 0, 1, 0},
            {1, 0, 1, 1},
            {1, 1, 1, 1}
        };
        
        static const float one_ninth = 1.0f / 9.0f;
        static const float weight[10] = {
                one_ninth,
                one_ninth,
                one_ninth,
                0.5f * one_ninth,
                one_ninth,
                0.5f * one_ninth,
                one_ninth,
                one_ninth,
                one_ninth,
                one_ninth
        };
        // iterate each combination
        const OpenMesh::Vec3f (*first_array)[2];
        const OpenMesh::Vec3f (*second_array)[2];
        if(first < 0){
            first_array = one;
            first = 0;
        }else if(first < 3){
            first_array = f_ij;
        }else{
            first_array = f_ji;
            first -= 3;
        }
        
        if(second < 3){
            second_array = f_ij;
        }else{
            second_array = f_ji;
            second -= 3;
        }
        float value = 0.0;//result
        for(int cid = 0; cid < 10; ++cid){
            float a = first_array[ uv_integrate_id[cid][0] ][ uv_integrate_id[cid][1] ][first];
            float b = second_array[ uv_integrate_id[cid][2] ][ uv_integrate_id[cid][3] ][second];
            value += a * b * weight[cid];
        }
        return value;
    }
    
};
static void build_problem_Eigen(const int n6, const Mesh &mesh, const OpenMesh::HPropHandleT<PrismProperty> &P_PrismProperty, 
                            const std::vector<OpenMesh::FaceHandle> &face_handles, const std::unordered_map<int,int> &face_idx_2_i, 
                            SpMat &B, 
                            Eigen::VectorXf &negA_T){
    std::unordered_set<int> he_id_set;
    //////////////////////////////////////////////////////////////////////////////
    //std::cout<< "B:\n" <<  B <<std::endl;
    //std::cout<< "-A^T:\n" << negA_T << std::endl;
    //////////////////////////////////////////////////////////////////////////////
    for(int i = 0; i < face_handles.size(); ++i){
        // iterate all faces
        const OpenMesh::FaceHandle &fh_i = face_handles[i];
        const int f_i_id = fh_i.idx();
        for(Mesh::ConstFaceHalfedgeIter fhe_it = mesh.cfh_iter(fh_i); fhe_it.is_valid(); ++fhe_it){
            // Grab the opposite half edge and face
            const Mesh::HalfedgeHandle he_i = *fhe_it;
            Mesh::HalfedgeHandle he_j = mesh.opposite_halfedge_handle(he_i);
		    Mesh::FaceHandle fh_j = mesh.opposite_face_handle(*fhe_it);
            // if he_i is a boundary halfedge, there is no opposite prism face,
            // which means that it has no contribution to the total energy E = Sum(w_ij * E_ij)
            // we could skip this half edge he_i now
            if (!he_j.is_valid() || mesh.is_boundary(he_i) || !fh_j.is_valid()){
                #ifndef NDEBUG
			    std::cout << "halfedge's opposite doesnot exist, idx = "<< he_i.idx() << std::endl;
                #endif
			    continue;
		    }

            // for same pair half edge Eij = Eji, we only want to calculate it once
            // (he_i, he_j) == (he_j, he_i)
            const int he_i_id = he_i.idx();
            const int he_j_id = he_j.idx();
            
            const bool he_i_in_set = (he_id_set.find(he_i_id) != he_id_set.end());
            if(he_i_in_set){
                #ifndef NDEBUG
                const bool he_j_in_set = (he_id_set.find(he_j_id) != he_id_set.end());
                // assert for debug, he pair should be both in set or neither in set 
                assert(he_i_in_set && he_j_in_set);
                #endif
                continue;
            }
            // this pair is visited now, put them into set
            he_id_set.insert(he_i_id);
            he_id_set.insert(he_j_id);

            // get the f^ij_[0/1][0/1] in the PriMo equation
            const int f_j_id = fh_j.idx();
            const PrismProperty * const P_i = &(mesh.property(P_PrismProperty, he_i));
            const PrismProperty * const P_j = &(mesh.property(P_PrismProperty, he_j));
            const float w_ij = P_i->weight_ij;
            // assert for debug, opposite half edges should have same edge weight
            assert(fabs(w_ij - P_j->weight_ij) < FLT_EPSILON);

            // get all p^ij p^ji that are used to fill in matrices
            Pij_ji p(P_i, P_j);
            
            // boundary contidion: opposite face is not optimizable,
            // which has a simpler method to fill in matrices more efficiently
            // pij(x, y, z)   pji(x, y, z)  one(1,  1,  1)
            //     0  1  2        3  4  5      -1  -1  -1
            SpMat current_B(n6, n6);
            SpMat current_negA_T(n6, 1);
            Eigen::Vector3f N, M;
            N << p(-1, 0) - p(-1, 3), p(-1, 1) - p(-1, 4), p(-1, 2) - p(-1, 5);
            M << -p(2,4)+p(1,5), p(2,3)-p(0,5), -p(1,3)+p(0,4);
            M *= w_ij;
            N *= w_ij;
            if(face_idx_2_i.find(f_j_id) == face_idx_2_i.end()){
                // boundary case
                Eigen::Matrix<float, 6, 6> DTD; 
                DTD<< 
                p(1,1)+p(2,2),           -p(0,1),        -p(0,2),                 0,         -p(-1,2),          p(-1,1),
                            0,     p(0,0)+p(2,2),        -p(1,2),           p(-1,2),                0,         -p(-1,0),
                            0,                 0,  p(0,0)+p(1,1),          -p(-1,1),          p(-1,0),                0,
                            0,                 0,              0,                 1,                0,                0,
                            0,                 0,              0,                 0,                1,                0,
                            0,                 0,              0,                 0,                0,                1;
               
                // copy the upper part to become a symmetric matrix
                DTD = DTD.selfadjointView<Eigen::Upper>();
                
                //////////////////////////////////////////////////////////////////////////////
                // std::cout<< "boundary DTD:\n" <<  DTD <<std::endl;
                //////////////////////////////////////////////////////////////////////////////
                // push DTD into current_B
                current_B.reserve(Eigen::VectorXi::Constant(n6, 6));
                const int start_rc = i * 6;
                const int end_rc   = start_rc + 6;
                for(int r = start_rc, dtd_i = 0; dtd_i < 6; ++r, ++dtd_i){
                    for(int c = start_rc, dtd_j = 0; dtd_j < 6; ++c, ++dtd_j){
                        current_B.insert(r, c) = DTD(dtd_i, dtd_j);
                    }
                }
                // init current_negA_T
                current_negA_T.reserve(Eigen::VectorXi::Constant(1, 6));
                current_negA_T.insert(start_rc    , 0) =  M(0); 
                current_negA_T.insert(start_rc + 1, 0) =  M(1); 
                current_negA_T.insert(start_rc + 2, 0) =  M(2); 
                current_negA_T.insert(start_rc + 3, 0) = -N(0); 
                current_negA_T.insert(start_rc + 4, 0) = -N(1); 
                current_negA_T.insert(start_rc + 5, 0) = -N(2); 
            }else{
                // general case
                Eigen::Matrix<float, 12, 12> DTD;
                DTD<<
                p(1,1)+p(2,2),           -p(0,1),        -p(0,2),                 0,         -p(-1,2),          p(-1,1),   -p(1,4)-p(2,5),          p(1,3),             p(2,3),                0,          p(-1,2),         -p(-1,1),
                            0,     p(0,0)+p(2,2),        -p(1,2),           p(-1,2),                0,         -p(-1,0),           p(0,4),  -p(0,3)-p(2,5),             p(2,4),         -p(-1,2),                0,          p(-1,0),
                            0,                 0,  p(0,0)+p(1,1),          -p(-1,1),          p(-1,0),                0,           p(0,5),          p(1,5),     -p(0,3)-p(1,4),          p(-1,1),         -p(-1,0),                0,
                            0,                 0,              0,                 1,                0,                0,                0,        -p(-1,5),            p(-1,4),               -1,                0,                0,    
                            0,                 0,              0,                 0,                1,                0,          p(-1,5),               0,           -p(-1,3),                0,               -1,                0,
                            0,                 0,              0,                 0,                0,                1,         -p(-1,4),         p(-1,3),                  0,                0,                0,               -1,
                            0,                 0,              0,                 0,                0,                0,    p(4,4)+p(5,5),         -p(3,4),            -p(3,5),                0,         -p(-1,5),          p(-1,4),
                            0,                 0,              0,                 0,                0,                0,                0,   p(3,3)+p(5,5),            -p(4,5),          p(-1,5),                0,         -p(-1,3),
                            0,                 0,              0,                 0,                0,                0,                0,               0,      p(3,3)+p(4,4),         -p(-1,4),          p(-1,3),                0,
                            0,                 0,              0,                 0,                0,                0,                0,               0,                  0,                1,                0,                0,                                        
                            0,                 0,              0,                 0,                0,                0,                0,               0,                  0,                0,                1,                0,            
                            0,                 0,              0,                 0,                0,                0,                0,               0,                  0,                0,                0,                1;

                // copy the upper part to become a symmetric matrix
                DTD = DTD.selfadjointView<Eigen::Upper>();
                
                //////////////////////////////////////////////////////////////////////////////
                //std::cout<< "DTD:\n" <<  DTD <<std::endl;
                //////////////////////////////////////////////////////////////////////////////
                // push DTD into current_B
                current_B.reserve(Eigen::VectorXi::Constant(n6, 12));
                const int start_I = i * 6;
                //const int   end_I = start_I + 6;
                const int start_J = face_idx_2_i.at(f_j_id) * 6;
                //const int   end_J = start_J + 6;

                for(int r = start_I, dtd_i = 0; dtd_i < 6; ++r, ++dtd_i){
                    for(int c = start_I, dtd_j = 0; dtd_j < 6; ++c, ++dtd_j){
                        current_B.insert(r, c) = DTD(dtd_i, dtd_j);
                    }
                }

                for(int r = start_I, dtd_i = 0; dtd_i < 6; ++r, ++dtd_i){
                    for(int c = start_J, dtd_j = 6; dtd_j < 12; ++c, ++dtd_j){
                        current_B.insert(r, c) = DTD(dtd_i, dtd_j);
                    }
                }
                for(int r = start_J, dtd_i = 6; dtd_i < 12; ++r, ++dtd_i){
                    for(int c = start_I, dtd_j = 0; dtd_j < 6; ++c, ++dtd_j){
                        current_B.insert(r, c) = DTD(dtd_i, dtd_j);
                    }
                }
                for(int r = start_J, dtd_i = 6; dtd_i < 12; ++r, ++dtd_i){
                    for(int c = start_J, dtd_j = 6; dtd_j < 12; ++c, ++dtd_j){
                        current_B.insert(r, c) = DTD(dtd_i, dtd_j);
                    }
                }
                // init current_negA_T
                current_negA_T.reserve(Eigen::VectorXi::Constant(1, 12));
                current_negA_T.insert(start_I    , 0) =  M(0); 
                current_negA_T.insert(start_I + 1, 0) =  M(1); 
                current_negA_T.insert(start_I + 2, 0) =  M(2); 
                current_negA_T.insert(start_I + 3, 0) = -N(0); 
                current_negA_T.insert(start_I + 4, 0) = -N(1); 
                current_negA_T.insert(start_I + 5, 0) = -N(2);

                current_negA_T.insert(start_J    , 0) = -M(0); 
                current_negA_T.insert(start_J + 1, 0) = -M(1); 
                current_negA_T.insert(start_J + 2, 0) = -M(2); 
                current_negA_T.insert(start_J + 3, 0) =  N(0); 
                current_negA_T.insert(start_J + 4, 0) =  N(1); 
                current_negA_T.insert(start_J + 5, 0) =  N(2); 

            }
            //////////////////////////////////////////////////////////////////////////////
            //std::cout<< "B:\n" <<  B <<std::endl;
            //std::cout<< "-A^T:\n" << negA_T << std::endl;
            //////////////////////////////////////////////////////////////////////////////
            B += current_B * w_ij;
            negA_T += current_negA_T;
            //////////////////////////////////////////////////////////////////////////////
            //std::cout<< "B:\n" <<  B <<std::endl;
            //std::cout<< "-A^T:\n" << negA_T << std::endl;
            //////////////////////////////////////////////////////////////////////////////
            

            // // iterate each combination
            // for(int cid = 0; cid < 10; ++cid){
            //     // follow the convention of ZJW's note
            //     // #TODO[ZJW]: Double Check and add note link
            //     const OpenMesh::Vec3f &a = f_ij[ uv_integrate_id[cid][0] ][ uv_integrate_id[cid][1] ];
            //     const OpenMesh::Vec3f &b = f_ji[ uv_integrate_id[cid][0] ][ uv_integrate_id[cid][1] ];
            //     const OpenMesh::Vec3f &c = f_ij[ uv_integrate_id[cid][2] ][ uv_integrate_id[cid][3] ];
            //     const OpenMesh::Vec3f &d = f_ji[ uv_integrate_id[cid][2] ][ uv_integrate_id[cid][3] ];
            //     const float lambda = lambdas[cid];

            //     // Compute SparseMatrix D_ab, D_cd in ZJW's note
                
            //     SpMat D_ab_T(n6, 3);
            //     SpMat D_cd_T(n6, 3);
            //     // Boundary condition: one of the face(prisms)'s velocity(w, v) should be zero 
            //     D_ab_T.reserve(Eigen::VectorXi::Constant(3, std::min(10, n6)));
            //     D_cd_T.reserve(Eigen::VectorXi::Constant(3, std::min(10, n6)));

            //     // fill_in_D_T(a, b, f_i_id, f_j_id, D_ab_T);
            //     // fill_in_D_T(c, d, f_i_id, f_j_id, D_cd_T);
            //     fill_in_D_T(a, f_i_id, true, D_ab_T);
            //     fill_in_D_T(c, f_i_id, true, D_cd_T);
            //     if(face_id_set.find(f_j_id) != face_id_set.end()){
            //         // opposite is optimizable face, fill it in
            //         fill_in_D_T(b, f_j_id, false, D_ab_T);
            //         fill_in_D_T(d, f_j_id, false, D_cd_T);
            //     }
            //     //////////////////////////////////////////////////////////////////////////////
            //     std::cout<< "D_ab_T:\n" << D_ab_T << std::endl;
            //     std::cout<< "D_cd_T:\n" << D_cd_T << std::endl;
            //     //////////////////////////////////////////////////////////////////////////////
            //     SpMat D_ab = D_ab_T.transpose();
            //     SpMat D_cd = D_cd_T.transpose();
            //     // add contribution to -A^T ("b" in "Ax = b")
            //     OpenMesh::Vec3f lambda_b_minus_a_OpenMesh = lambda * (b - a);
            //     OpenMesh::Vec3f lambda_d_minus_c_OpenMesh = lambda * (d - c);
            //     Eigen::Vector3f lambda_b_minus_a_Eigen;
            //     Eigen::Vector3f lambda_d_minus_c_Eigen;
            //     lambda_b_minus_a_Eigen(0) = lambda_b_minus_a_OpenMesh[0];
            //     lambda_b_minus_a_Eigen(1) = lambda_b_minus_a_OpenMesh[1];
            //     lambda_b_minus_a_Eigen(2) = lambda_b_minus_a_OpenMesh[2];
                
            //     lambda_d_minus_c_Eigen(0) = lambda_d_minus_c_OpenMesh[0];
            //     lambda_d_minus_c_Eigen(1) = lambda_d_minus_c_OpenMesh[1];
            //     lambda_d_minus_c_Eigen(2) = lambda_d_minus_c_OpenMesh[2];
            //     negA_T += D_cd_T * lambda_b_minus_a_Eigen + D_ab_T * lambda_d_minus_c_Eigen;
            //     // add contribution to B + B^T ("A" in "Ax = b")
            //     B_add_BT += D_ab_T * D_cd + D_cd_T * D_ab;
            // }
        }
    }
    
}
void PrimoMeshViewer::global_optimize_faces(const std::vector<OpenMesh::FaceHandle> &face_handles, 
										const std::unordered_map<int,int> &face_idx_2_i)
{
    /* #TODO[ZJW][QYZ]: Here we are using Eigen to solve the SPD(symmetric positive definite) linear system.
       if it is the speed bottleneck, try SuiteSparse(even cuda - GPU implementation) with more pain :)
       Also, here we are using float instead of double, need to make sure it is sufficient. 
    */
    /* build the linear system. Here we follow the convention in 
       [PLH02]:http://citeseerx.ist.psu.edu/viewdoc/download;jsessionid=F1D81AF3257335BC6E902048DF161317?doi=10.1.1.6.5569&rep=rep1&type=pdf
        B C = -A^T. #TODO[ZJW]: add link of supplemental notes for explanation
    */
    // debug for that face_handles and face_idx_2_i data matches
    
    #ifndef NDEBUG
        std::cout << "face_handles size:" << face_handles.size() << std::endl;
        std::cout << "face_idx_2_i size:" << face_idx_2_i.size() << std::endl;
        assert(face_handles.size() == face_idx_2_i.size());
		for(int i = 0; i < face_handles.size(); ++i){
            // int idx = face_handles[i].idx();
            assert(face_idx_2_i.at(face_handles[i].idx()) == i);
        }
    #endif
    int n6 = (int)face_handles.size() * 6;
    // -A^T ("b" in "Ax = b"), it is init to zero.
    Eigen::VectorXf negA_T = Eigen::VectorXf::Zero(n6);
    // B("A" in "Ax = b")
    SpMat B(n6, n6);
    
    //  
    build_problem_Eigen(n6, mesh_, P_PrismProperty, face_handles, face_idx_2_i, B, negA_T);
    //////////////////////////////////////////////////////////////////////////////
    //std::cout<< "B:\n" <<  B<<std::endl;
    //std::cout<< "-A^T:\n" << negA_T << std::endl; 
    //////////////////////////////////////////////////////////////////////////////
    // solve the linear system 
    // #TODO[ZJW]: need look at the other Cholesky factorization in Eigen
    printf("FINISH BUILD\n");
    Eigen::SimplicialCholesky<SpMat> solver(B);  // performs a Cholesky factorization of B
    printf("FINISH DECOMPOSE\n");

    //solver.compute(B);
    // decompose should be success
    assert(solver.info() == Eigen::Success);
    
    Eigen::VectorXf x = solver.solve(negA_T);    // use the factorization to solve for the given right hand side
    printf("FINISH SOLVE\n");

    //////////////////////////////////////////////////////////////////////////////
    std::cout<< "x:\n"<< x << std::endl;
    //////////////////////////////////////////////////////////////////////////////

    // #TODO[ZJW]: update vertices position based on faces(prisms) around each vertex
   
    // update OpenMesh's normals
    mesh_.update_normals(); 
}
