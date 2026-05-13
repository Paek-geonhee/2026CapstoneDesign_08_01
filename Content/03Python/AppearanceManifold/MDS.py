import numpy as np
from scipy.sparse.linalg import eigsh
import matplotlib.pyplot as plt

def Get_MDS_graph(D, dim=3):

    """
    Params
    ----------
    D               : np.array(N,7)
    dim             : int

    return
    Z               : np.array(N,3)
    ----------
    """

    N = D.shape[0]
    #print("D.shape[0] : " + str(N))

    D = D.copy()
    #print("D: " + str(D))

    finite_mask = np.isfinite(D)

    D[~finite_mask] = np.max(D[finite_mask])

    H = np.eye(N) - np.ones((N, N)) / N

    B = -0.5 * H @ (D**2) @ H

    B = np.nan_to_num(B)

    B = (B + B.T) * 0.5

    evals, evecs = eigsh(B,k=dim,which='LA')

    idx = np.argsort(evals)[::-1]

    evals = evals[idx]
    evecs = evecs[:, idx]

    Z = (evecs * np.sqrt(np.maximum(evals, 0)))

    return Z



def visualize_MDS_Graph(Z, data_7d):
    """
    Params
    ----------
    Z               : np.array(N,3)
    data_7d         : np.array(N,7)
    """
     

    roughness = data_7d[:, 6]
    colors = data_7d[:, 0:3]

    fig = plt.figure()
    ax = fig.add_subplot(111, projection='3d')

    ax.scatter(Z[:, 0], Z[:, 1], Z[:, 2], c=colors, s=1)

    plt.show()