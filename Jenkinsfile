#!/usr/bin/env groovy

def buildNumber = env.BUILD_NUMBER as int
if (buildNumber > 1) milestone(buildNumber - 1)
milestone(buildNumber)

pipeline {
    agent none
    stages {
        stage('Build and check') {
            agent {
                dockerfile {
                    filename 'Dockerfile.build'
                    args '-e PYTHONDONTWRITEBYTECODE=1'
                    label 'docker'
                    additionalBuildArgs '--build-arg=EXTRA_PACKAGES="libkustomer-dev"'
                }
            }
            stages {
                stage('Lint') {
                    steps {
                        echo 'Linting..'
                        sh 'flake8 -v --format=pylint swig/python/kopano/kopano > pylint.log || true'
                        recordIssues tool: pyLint(pattern: 'pylint.log'), qualityGates: [[threshold: 5000, type: 'TOTAL', unstable: true]]
                    }
                }
                stage('Build') {
                    steps {
                        echo 'Building..'
                        sh './bootstrap.sh'
                        sh './configure --enable-release --enable-pybind --enable-kcoidc --enable-kustomer TCMALLOC_CFLAGS=" " TCMALLOC_LIBS="-ltcmalloc_minimal" PYTHON="$(which python3)" PYTHON_CFLAGS="$(pkg-config python3 --cflags)" PYTHON_LIBS="$(pkg-config python3 --libs)"'
                        sh 'make -j $(nproc)'
                        recordIssues(tools: [gcc()])
                    }
                }
                stage('Check') {
                    steps {
                        echo 'Checking..'
                        sh 'make check'
                    }
                }
            }
        }
    }
}
