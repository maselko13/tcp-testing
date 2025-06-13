# tcp-testing
This repository contains the code utilized for testing BBRv1 and CUBIC under variable delay and bandwidth in-transmission in my Bachelor's Thesis.

In order to reproduce the experiments contained within said thesis:

1. Download and set up the ns-3 environment - information on how to do this can be found here [https://www.nsnam.org/docs/tutorial/html/](https://www.nsnam.org/docs/tutorial/html/)
2. paste in (so either replace or create) the scratch folder in the directory containing ns-3 with the scratch folder from this repository.
3. in order to emulate connections with proper parameters, change the values according to the comments in code in tcp.cc
4. run the experiment via the ./ns3 run tcp command in the terminal you're using
5. the results will be shown in-terminal, as well as saved to .txt files in the "metrics" folder.

If there are any questions about the code or issues you might have found when reproducing the experiments, do let me know via e-mail or through github directly.

