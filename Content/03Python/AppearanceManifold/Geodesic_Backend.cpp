#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include <vector>
#include <queue>
#include <limits>
#include <cmath>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace py = pybind11;

struct Edge
{
    int dst;
    float weight;
};

using Graph = std::vector<std::vector<Edge>>;

static constexpr float INF =
    std::numeric_limits<float>::infinity();


// ============================================================
// Build Graph
// ============================================================

Graph BuildGraph(
    const int* edge_src,
    const int* edge_dst,
    const float* edge_weight,
    int E,
    int N
)
{
    Graph graph(N);

    for (int i = 0; i < E; ++i)
    {
        int src = edge_src[i];
        int dst = edge_dst[i];
        float w = edge_weight[i];

        graph[src].push_back({ dst, w });
        graph[dst].push_back({ src, w });
    }

    return graph;
}


// ============================================================
// Single Source Dijkstra
// ============================================================

void Dijkstra(
    const Graph& graph,
    int source,
    float* dist_out
)
{
    int N = static_cast<int>(graph.size());

    for (int i = 0; i < N; ++i)
    {
        dist_out[i] = INF;
    }

    dist_out[source] = 0.0f;

    using Node = std::pair<float, int>;

    std::priority_queue<
        Node,
        std::vector<Node>,
        std::greater<Node>
    > pq;

    pq.push({ 0.0f, source });

    while (!pq.empty())
    {
        auto [current_dist, u] = pq.top();
        pq.pop();

        if (current_dist > dist_out[u])
            continue;

        for (const auto& edge : graph[u])
        {
            int v = edge.dst;

            float next_dist =
                current_dist + edge.weight;

            if (next_dist < dist_out[v])
            {
                dist_out[v] = next_dist;

                pq.push({ next_dist, v });
            }
        }
    }
}


// ============================================================
// All Pairs Geodesic
// ============================================================

py::array_t<float>
ComputeGeodesicDistanceMatrix(
    py::array_t<int, py::array::c_style | py::array::forcecast> edge_src,
    py::array_t<int, py::array::c_style | py::array::forcecast> edge_dst,
    py::array_t<float, py::array::c_style | py::array::forcecast> edge_weight,
    int N
)
{
    auto src_buf = edge_src.unchecked<1>();
    auto dst_buf = edge_dst.unchecked<1>();
    auto weight_buf = edge_weight.unchecked<1>();

    int E = src_buf.shape(0);

    Graph graph = BuildGraph(
        src_buf.data(0),
        dst_buf.data(0),
        weight_buf.data(0),
        E,
        N
    );

    py::array_t<float> result({ N, N });

    auto result_buf = result.mutable_unchecked<2>();


    // ========================================================
    // Parallel APSP
    // ========================================================

    #pragma omp parallel for
    for (int source = 0; source < N; ++source)
    {
        std::vector<float> local_dist(N);

        Dijkstra(
            graph,
            source,
            local_dist.data()
        );

        for (int j = 0; j < N; ++j)
        {
            result_buf(source, j) =
                local_dist[j];
        }
    }

    return result;
}


// ============================================================
// PYBIND
// ============================================================

PYBIND11_MODULE(geodesic_backend, m)
{
    m.def(
        "compute_geodesic_distance_matrix",
        &ComputeGeodesicDistanceMatrix,
        py::arg("edge_src"),
        py::arg("edge_dst"),
        py::arg("edge_weight"),
        py::arg("N")
    );
}