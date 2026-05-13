from scipy.sparse import csr_matrix
import numpy as np
from scipy.sparse.csgraph import dijkstra

def Get_Geodesic_Distance_Matrix(edge_src, edge_dst, edge_weight, N):

    """
    Parameters
    ----------
    edge_src : (E,)
    edge_dst : (E,)
    edge_weight : (E,)
    N : int

    Return
    ------
    geodesic : np.array(E,)
    """

    #rows = np.concatenate([edge_src, edge_dst])
    #cols = np.concatenate([edge_dst, edge_src])
    #data = np.concatenate([edge_weight, edge_weight])

    rows = np.concatenate([edge_src, edge_dst]).astype(np.float32)
    cols = np.concatenate([edge_dst, edge_src]).astype(np.float32)
    data = np.concatenate([edge_weight, edge_weight]).astype(np.float32)

    #adj = coo_matrix((data, (rows, cols)), shape=(N, N))
    #adj = adj.tocsr()

    adj = csr_matrix((data, (rows, cols)), shape=(N, N))

    geodesic = dijkstra(adj, directed=False)

    return geodesic