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
                stage('Test (short)') {
                    steps {
                        echo 'Testing ..'
                        sh 'make test-short PYTEST=pytest-3 || true'
                        junit testResults: 'php-ext/test-short.log'
                        junit testResults: 'swig/python/kopano/test-short.xml'
                        junit testResults: 'swig/python/test-short.xml'
                    }
		}
            }
            post {
                success {
                        stash includes: '**', name: 'workspace'
                }
            }
        }
        stage('Test Suite') {
            agent {
                label 'docker'
            }
            stages {
                stage('Verify') {
                    steps {
                        echo 'Checking build...'
                        unstash 'workspace'
                    }
                }
                stage('Run Test Suite') {
                    steps {
                        echo 'Testing..'
                        sh 'make -C test test-backend-kopano-ci-run EXTRA_LOCAL_ADMIN_USER=$(id -u) DOCKERCOMPOSE_UP_ARGS=--build DOCKERCOMPOSE_EXEC_ARGS="-T -u $(id -u) -e HOME=/workspace" || true'
			junit testResults: 'php-ext/test.log'
			junit testResults: 'libicalmapi/test.xml'
			junit testResults: 'gateway/test.xml'
			junit testResults: 'spooler/python/test.xml'
                        junit testResults: 'swig/python/test.xml'
                        junit testResults: 'swig/python/kopano/test.xml'
                        junit testResults: 'test/test-admin.log.xml'
                        junit testResults: 'test/test-ectools.log.xml'
                    }
                }
            }
            post {
                always {
                    sh 'make -C test test-backend-kopano-ci-logs DOCKERCOMPOSE_LOGS_ARGS="--timestamps --no-color" || true'
                    sh 'make -C test test-backend-kopano-ci-clean'
                }
            }
        }
    }
}
