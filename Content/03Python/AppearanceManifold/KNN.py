from sklearn.neighbors import NearestNeighbors
import numpy as np
from WeatheringScore import compute_weather_score


def Get_KNN_graph(samples_7d, K=8, extra_ratio=4, epsilon_scale=1.5):
    """
    Params
    ----------
    samples_7d      : np.array(N,7)
    K               : int
    extra_ratio     : int
    epsilon_scale   : float

    return
    edge_src        : np.array(N,N)
    edge_dst        : np.array(N,N)
    edge_weight     : np.array(N,N)
    weather_score   : np.array(N,7)
    ----------
    """

    N = samples_7d.shape[0]

    weather_score = compute_weather_score(samples_7d)
    #print("Weather Score : ")
    #print(weather_score)

    extra_k = min(K * extra_ratio, N - 1)

    nn = NearestNeighbors(n_neighbors=extra_k + 1, algorithm='auto')

    nn.fit(samples_7d)

    distances, indices = nn.kneighbors(samples_7d)

    # remove self
    distances = distances[:, 1:]
    indices = indices[:, 1:]
    
    print("Distances :")
    print(distances)
    print()
    print("Indices :")
    print(indices)


    edge_src = []
    edge_dst = []
    edge_weight = []

    for i in range(N):

        local_dists = distances[i]
        local_inds = indices[i]

        dp = np.mean(local_dists[:K])

        epsilon = epsilon_scale * dp

        score_i = weather_score[i]

        for dist, j in zip(local_dists, local_inds):

            if dist > epsilon:
                break

            score_j = weather_score[j]

            delta = score_j - score_i

            if delta >= 0:
                # forward progression
                progression_factor = 1.0 - 0.35 * delta

            else:
                # backward penalty
                progression_factor = 1.0 + 1.5 * abs(delta)

            weight = dist * progression_factor

            progression_bonus = 1.0 - delta

            weight = dist * progression_bonus

            edge_src.append(i)
            edge_dst.append(j)
            edge_weight.append(weight)

    return (
        np.array(edge_src, dtype=np.int32),
        np.array(edge_dst, dtype=np.int32),
        np.array(edge_weight, dtype=np.float32),
        weather_score
    )