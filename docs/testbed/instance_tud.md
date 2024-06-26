# Instance at TU Dresden

In the second half of 2023 a (semi-)public instance of the Shepherd Testbed went live. This section of the documentation will be landing-page and inform users about the first steps.

Direct Link: <https://shepherd.cfaed.tu-dresden.de/>

## Deployment

The initial deployment covered the ring of offices around the buildings ventilation system. The inner structure mostly blocks RF due to lots of metal vents. 10 - 14 shepherd observers were used for the testrun. Unfortunately the RF-Performance of the nodes was not strong enough to close the gap between II62 and II75 (left side of plan). The Issue is documented [here](https://github.com/orgua/shepherd-targets/issues/3) and new targets with external antenna are in production.

For now (April 2024) the testbed was reshaped to mimic an elongated multihop mesh-network on the lower part of the office-floor.

Below is a screenshot of the [Campus-Navigator](https://navigator.tu-dresden.de/etplan/bar/02) with marked node-positions.

![cfaed floor with marked node-positions](./media/cfaed_floorplan_mod.png)

Most horizontal walls are concrete, while the walls between offices are drywall.

The link-matrix of the Testbed looks like that (values in dBm):

```

```

## Controlling the Testbed

Currently direct shell-access to the server is needed. From there [Shepherd-Herd](https://pypi.org/project/shepherd_herd) can be used to execute Tasks created by the [Core-Datalib](https://pypi.org/project/shepherd_core).

Top of the prio-list is to open an API-port to the internet. That would allow the testbed-client in the datalib to connect with the server remotely. In the near future each user-account can define experimental setups and the client transforms these to tasks, from patching the node-id into the firmware, over programming the targets, running the measurements and collecting the data for download.

Each Observer generates a hdf5-file. While we used shepherd in the past some postprocessing was generalized and bundled in the [main-datalib](https://pypi.org/project/shepherd_data). It is possible to extract logs, calculate metadata and generate plots.

## Example-Workflow

First create an experiment and transform it to a task-set for the Testbed. With the self-validating experiment we can generate a `TestbedTasks`-config.

```Python
from shepherd_core.data_models import GpioTracing
from shepherd_core.data_models.content import EnergyEnvironment
from shepherd_core.data_models.content import Firmware
from shepherd_core.data_models.content import VirtualSourceConfig
from shepherd_core.data_models.experiment import Experiment
from shepherd_core.data_models.experiment import TargetConfig
from shepherd_core.data_models.task import TestbedTasks

xp1 = Experiment(
    name="rf_survey",
    comment="generate link-matrix",
    duration=4 * 60,
    target_configs=[
        TargetConfig(
            target_IDs=list(range(3000, 3010)),
            custom_IDs=list(range(0, 99)),  # note: longer list is OK
            energy_env=EnergyEnvironment(name="eenv_static_3300mV_50mA_3600s"),
            virtual_source=VirtualSourceConfig(name="direct"),
            firmware1=Firmware(name="nrf52_rf_survey"),
            firmware2=Firmware(name="msp430_deep_sleep"),
            power_tracing=None,
            gpio_tracing=GpioTracing(),
        )
    ],
)
TestbedTasks.from_xp(xp1).to_file("./tb_tasks_rf_survey.yaml")
```

A more detailed guide for creating experiments is described in [](../user/experiments.md).

After transferring the config-file to the testbed-server it can be run:

```Shell
shepherd-herd run tb_tasks_rf_survey.yaml
```


## Related & Useful Links

- used [Hardware](../user/hardware) (Shepherd Cape, available Targets)
- available [firmwares for the targets](https://github.com/orgua/shepherd-targets)
    - adapted [Trafficbench](https://github.com/orgua/TrafficBench) for an [RF-survey](https://github.com/orgua/shepherd-targets/tree/main/nrf52_rf_survey)
- the [Trafficbench pythontool](https://pypi.org/project/trafficbench)

## Contributions

Feedback is more than welcome during that initial phase. Same for reusable & useful scripts or firmware you developed and want to donate.
