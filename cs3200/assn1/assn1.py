# Auther: Nathan Bal
# Analytical Value: 19.03595°C

import matplotlib.pyplot as plt
import numpy as np

def forward_euler_algorithm(T_initial, T_surround, h, r, t_final):
    """
    Forward Euler solver for Newton's law of cooling.
    
    Parameters
    ----------
    T_initial : Initial temperature of the object
    T_surround : Ambient/ environment temperature
    r : Cooling coefficient
    h : Time step size (s)
    t_final : Total simulation time (min)
    
    Returns
    -------
    temps and times : Prints the results and returns the array for the times and the temps at each time
    """
    t = 0.0
    T = T_initial
    temps = [T]
    times = [0.0]

    while t <  (t_final * 60):
        T = T - (h * r) * (T - T_surround)
        t += h
        temps.append(T)
        times.append(t)

    print(f"\nThe temperature of the coffee cup after {t_final} minutes with an initial temp of {T_initial}°C,")
    print(f"surrounding temp of {T_surround}°C, and a stepsize h of {h} seconds is T final of {T:.5f}°C\n")
    return temps, times

def euler_trapezoidal_algorithm(T_initial, T_surround, h, r, t_final):
    """
    Euler Trapezoidal solver for Newton's law of cooling.
    
    Parameters
    ----------
    T_initial : Initial temperature of the object
    T_surround : Ambient/ environment temperature
    r : Cooling coefficient
    h : Time step size (s)
    t_final : Total simulation time (min)
    
    Returns
    -------
    temps and times : Prints the results and returns the array for the times and the temps at each time
    """
    t = 0
    T_n = T_initial
    temps = [T_n]
    times = [0.0]

    while t <  (t_final * 60):
        T_nPlus1 = T_n - (h * r) * (T_n - T_surround)       # Also equal to T_k

        k = 0
        tolerance = 1
        T_k = T_nPlus1
        while tolerance > 0.05:    # 0.05 is the tolerance
            T_kPlus1 = T_n + (h/2) * ((-r * (T_n - T_surround)) + (-r * (T_k - T_surround)))
            tolerance = abs(T_kPlus1 - T_k)
            T_k = T_kPlus1
            k += 1

        T_n = T_k
        t += h
        temps.append(T_n)
        times.append(t)

    print(f"\nThe temperature of the coffee cup after {t_final} minutes with an initial temp of {T_initial}°C,")
    print(f"surrounding temp of {T_surround}°C, and a stepsize h of {h} seconds is T final of {T_n:.5f}°C\n")

    return temps, times

def analytical_solution(T_initial, T_surround, r, t_array):
    """
    Analytical solution for Newton's law of cooling. This is used to graph the true
    solution to the cooling problem for comparison in the plot.
    """
    return T_surround + (T_initial - T_surround) * np.exp(-r * t_array)

def plot_results(algorithm, T_initial, T_surround, r, t_final, h_list):
    """
    Creates the plot for either the trapezoidal or forward method depending on the
    first parameter. Creates a line for each step size specified.
    """
    plt.figure(figsize=(10,6))
    
    if algorithm == "Euler Trapezoidal":
        for h in h_list:
            temps, times = euler_trapezoidal_algorithm(T_initial, T_surround, h, r, t_final)
            plt.plot(times, temps, marker='o', linestyle='-', label=f'h={h}s', markersize=2)
    elif algorithm == "Forward Euler":
        for h in h_list:
            temps, times = forward_euler_algorithm(T_initial, T_surround, h, r, t_final)
            plt.plot(times, temps, marker='o', linestyle='-', label=f'h={h}s', markersize=2)

    t_analytical = np.linspace(0, t_final*60, 1000)
    T_exact = analytical_solution(T_initial, T_surround, r, t_analytical)
    plt.plot(t_analytical, T_exact, 'k--', label='Analytical', linewidth=2)


    plt.xlabel('Time (s)')
    plt.ylabel('Temperature (°C)')
    plt.title(f'Cooling of Coffee Cup: {algorithm} Method')
    plt.legend()
    plt.grid(True)
    plt.savefig(f"{algorithm}_plot.png", dpi=300)
    plt.show()


if __name__ == "__main__":
    T_surround = 19
    T_initial = 84
    r = 0.025
    t_final = 5     # Minutes
    algorithm = "Euler Trapezoidal"

    h_list = (0.25, 0.5, 1, 5, 10, 15, 30)       # Seconds

    # Switch the Algorithm to either "Euler Trapezoidal" or "Forward Euler" to produce different graphs
    print(f"\n--- {algorithm} Method ---")
    plot_results(algorithm, T_initial, T_surround, r, t_final, h_list)