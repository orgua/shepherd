# Contributing

This section helps developers getting started with contributing to `shepherd`.

## Codestyle

Please stick to the C and Python codestyle guidelines provided with the source code.

All included **Python code** uses the feature-set of **version 3.10** and is supposed to be formatted & linted using [ruff](https://docs.astral.sh/ruff/) for clean and secure code.

**C code** uses the feature-set of **C99** and shall be formatted based on *LLVM*-Style with some alterations to make it easier to read, similar to python code.
We provide the corresponding `clang-format` config as `.clang-format` in the repository's root directory.

Many IDEs/editors allow to automatically format code using the corresponding formatter and codestyle.

To ensure quality standards we implemented the [pre-commit](https://pre-commit.com/)-workflow into the repo. It will

- handle formatting for python and C code (automatically)
- lint python, C, YAML, TOML, reStructuredText (rst), ansible playbooks
- warns about security-related issues and deprecated features in python and C code

Pull Requests to the main branch will be tested online with **GitHub Actions**.

To run it on your own make sure you have pre-commit installed:

```shell
pip3 install pre-commit
sudo apt install cppcheck
```

Now you can run the pre-commit checks:

```Shell
pre-commit run --all-files
# or in short
pre-commit run -a
```

## Development setup

While some parts of the `shepherd` software stack can be developed hardware independent, in most cases you will need to develop/test code on the actual target hardware.

We found the following setup convenient: Have the code on your laptop/workstation and use your editor/IDE to develop code.
Have a BeagleBone (potentially with `shepherd` hardware) connected to the same network as your workstation.
Prepare the BeagleBone by running the `bootstrap.yml` ansible playbook to allow passwordless entry.

### Option 1

You can now use the integrated functionally of the `deploy/dev_rebuild_sw` playbook that can push the changed files to the target and builds and installs it there with needing a reboot.
Running the playbook takes some minutes, as all software components (kernel module, firmware and python package) are rebuilt.

```shell
cd shepherd
ansible-playbook deploy/dev_rebuild_sw.yml
# if transfer of host-files is desired, answer yes on the prompt
```

### Option 2

Some IDEs/editors allow to automatically push changes via ssh / scp to the target. The directory `/opt/shepherd` is used as the projects root-dir on the beaglebone.
In addition, the playbook `deploy/dev_rebuild_sw.yml` builds and installs all local source on target (conveniently without a restart) or you can update only parts of it manually. You can have a look at `deploy/roles/sheep/tasks/build_shp.yml` to see the commands needed.

### Option 3

You can mirror your working copy of the `shepherd` code to the BeagleBone using a network file system.
We provide a playbook (`deploy/setup-dev-nfs.yml`) to conveniently configure an `NFS` share from your local machine to the BeagleBone.
After mounting the share on the BeagleBone, you can compile and install the corresponding software component remotely over ssh on the BeagleBone while editing the code locally on your machine.
Or you use the playbook described in [](#option-1).


## Build the docs

**Note**: Docs are automatically built with GitHub actions after changes on main-branch.

Make sure you have the python requirements installed:

```shell
pip install --upgrade pip pipenv wheel setuptools

pipenv install
```

Activate the `pipenv` environment:

```shell
pipenv shell
```

Change into the docs directory and build the html documentation

```shell
cd docs
make html
```

The build is found at `docs/_build/html`. You can view it by starting a simple http server:

```shell
cd _build/html
python -m http.server
```

Now navigate your browser to `localhost:8000` to view the documentation.
As an alternative it often suffices to just pull the `index.html` into a browser of your choice.

## Tests

There is a testing framework that covers a large portion of the python code.
You should always make sure the tests are passing before committing your code.
When changing lower level code it is also recommended to run the test-benches of the higher level tools.
For a tutorial see the dedicated sections in the tool-documentations:

- [sheep-testbench](../tools/sheep.md#unittests)
- [herd-testbench](../tools/herd.md#unittests)

## Releasing

Before committing to the repository please run our [pre-commit](https://pre-commit.com/)-workflow described in [](#codestyle).

Once you have a clean, stable and tested version, you should decide if your release is a patch, minor or major update (see [Semantic Versioning](https://semver.org/)).
Use `bump2version` to update the version number across the repository:

```shell
pipenv shell
pre-commit run --all-files
bump2version --allow-dirty --new-version 0.9.0 patch
# version-format: major.minor.patch
```

Finally, open a pull-request to allow merging your changes into the main-branch and to trigger the test-pipeline.
